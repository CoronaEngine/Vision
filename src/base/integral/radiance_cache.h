//
// Created by Zero on 2025/7/16.
//

#pragma once

#include "base/node.h"
#include "base/encoded_object.h"
#include "rhi/common.h"

namespace vision {

class Pipeline;

class Sampler;

class RenderEnv;

class RadianceCache : public Node {
private:
public:
    using Desc = RadianceCacheDesc;
    RadianceCache() = default;
    explicit RadianceCache(const Desc &desc)
        : Node(desc) {}
};

}// namespace vision