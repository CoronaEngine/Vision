//
// Created by Zero on 2023/6/2.
//

#include "base/bake_utlis.h"
#include "ext/xatlas/xatlas.h"

namespace vision {
using namespace ocarina;

class Stopwatch {
public:
    Stopwatch() { reset(); }
    void reset() { m_start = clock(); }
    double elapsed() const { return (clock() - m_start) * 1000.0 / CLOCKS_PER_SEC; }

private:
    clock_t m_start;
};

static bool progress_callback(xatlas::ProgressCategory category, int progress, void *userData) {
    // Don't interupt verbose printing.
    Stopwatch *stopwatch = (Stopwatch *)userData;
    static std::mutex progressMutex;
    std::unique_lock<std::mutex> lock(progressMutex);
    if (progress == 0) {
        stopwatch->reset();
    }
    OC_INFO_FORMAT("{} {} %", xatlas::StringForEnum(category), progress);
    if (progress == 100) {
        OC_INFO_FORMAT("{} seconds ({} ms) elapsed", stopwatch->elapsed() / 1000.0, stopwatch->elapsed());
    }
    return true;
}

class XAtlas : public UVUnwrapper {
private:
    xatlas::Atlas *_atlas{};
    Stopwatch _global_stopwatch;
    Stopwatch _stopwatch;
    vector<float3> _pos;

public:
    explicit XAtlas(const UVUnwrapperDesc &desc)
        : UVUnwrapper(desc),
          _atlas(nullptr) {
    }

    struct Guard {
        XAtlas *spreader{};
        explicit Guard(XAtlas *spreader) : spreader(spreader) { spreader->init_xatlas(); }
        ~Guard() { spreader->destroy_xatlas(); }
    };

    void destroy_xatlas() {
        if (_atlas) {
            xatlas::Destroy(_atlas);
            _atlas = nullptr;
        }
    }

    void init_xatlas() {
        _atlas = xatlas::Create();
        xatlas::SetProgressCallback(_atlas, progress_callback, &_stopwatch);
    }

    [[nodiscard]] static xatlas::MeshDecl mesh_decl(const vision::Mesh &mesh) {
        xatlas::MeshDecl ret;
        const vector<Vertex> &vertices = mesh.vertices();
        const vector<Triangle> &triangle = mesh.triangles();

        // fill position
        ret.vertexCount = vertices.size();
        ret.vertexPositionData = vertices.data();
        ret.vertexPositionStride = sizeof(Vertex);

        // fill normal
        ret.vertexNormalData = reinterpret_cast<const std::byte *>(vertices.data()) + Vertex::n_offset();
        ret.vertexNormalStride = sizeof(Vertex);

        // fill normal
        ret.vertexNormalData = reinterpret_cast<const std::byte *>(vertices.data()) + Vertex::n_offset();
        ret.vertexNormalStride = sizeof(Vertex);

        // fill tex_coord
        ret.vertexUvData = reinterpret_cast<const std::byte *>(vertices.data()) + Vertex::uv_offset();
        ret.vertexUvStride = sizeof(Vertex);

        // fill indices
        ret.indexCount = triangle.size() * 3;
        ret.indexData = triangle.data();
        ret.indexFormat = xatlas::IndexFormat::UInt32;

        OC_INFO_FORMAT("xatlas mesh decl vertex num is {}, triangle num is {}", vertices.size(), triangle.size());

        return ret;
    }

    [[nodiscard]] xatlas::PackOptions pack_options(const Mesh *shape) const noexcept {
        xatlas::PackOptions ret;
        ret.padding = _padding;
        ret.resolution = ocarina::clamp(uint(shape->lightmap_size() * _scale), _min, _max);
        ret.bruteForce = true;
        return ret;
    }

    [[nodiscard]] xatlas::ChartOptions chart_options() const noexcept {
        xatlas::ChartOptions ret;
        return ret;
    }

    [[nodiscard]] UnwrapperResult apply(const Mesh *shape) override {
        Guard __(this);
        UnwrapperResult unwrapper_result;
        xatlas::MeshDecl decl = mesh_decl(*shape);
        xatlas::AddMeshError error = xatlas::AddMesh(_atlas, decl, 1);
        if (error != xatlas::AddMeshError::Success) {
            destroy_xatlas();
            OC_ERROR("xatlas adding mesh error");
        }

        xatlas::AddMeshJoin(_atlas);
        xatlas::Generate(_atlas, chart_options(), pack_options(shape));
        unwrapper_result.width = _atlas->width;
        unwrapper_result.height = _atlas->height;
        for (int i = 0; i < _atlas->meshCount; ++i) {
            xatlas::Mesh &mesh = _atlas->meshes[i];
            UnwrapperMesh u_mesh;
            for (int j = 0; j < mesh.vertexCount; ++j) {
                xatlas::Vertex &vertex = mesh.vertexArray[j];
                u_mesh.vertices.emplace_back(make_float2(vertex.uv[0], vertex.uv[1]),
                                             vertex.xref, vertex.chartIndex);
            }

            for (int j = 0; j < mesh.indexCount; j += 3) {
                uint i0 = mesh.indexArray[j];
                uint i1 = mesh.indexArray[j + 1];
                uint i2 = mesh.indexArray[j + 2];
                u_mesh.triangles.emplace_back(i0, i1, i2);
            }
            unwrapper_result.meshes.push_back(u_mesh);
        }
        return unwrapper_result;
    }
};

}// namespace vision

VS_MAKE_CLASS_CREATOR(vision::XAtlas)