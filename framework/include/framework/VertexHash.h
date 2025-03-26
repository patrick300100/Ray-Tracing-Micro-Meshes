#pragma once

struct VertexHash {
    size_t operator()(const Vertex& v) const {
        size_t seed = 0;
        hash_combine(seed, v.position.x);
        hash_combine(seed, v.position.y);
        hash_combine(seed, v.position.z);
        hash_combine(seed, v.normal.x);
        hash_combine(seed, v.normal.y);
        hash_combine(seed, v.normal.z);

        for(int j = 0; j < 4; j++) hash_combine(seed, v.baseBoneIndices0[j]);
        for(int j = 0; j < 4; j++) hash_combine(seed, v.baseBoneIndices1[j]);
        for(int j = 0; j < 4; j++) hash_combine(seed, v.baseBoneIndices2[j]);
        for(int j = 0; j < 4; j++) hash_combine(seed, v.baseBoneWeights0[j]);
        for(int j = 0; j < 4; j++) hash_combine(seed, v.baseBoneWeights1[j]);
        for(int j = 0; j < 4; j++) hash_combine(seed, v.baseBoneWeights2[j]);

        hash_combine(seed, v.baryCoords.x);
        hash_combine(seed, v.baryCoords.y);
        hash_combine(seed, v.baryCoords.z);

        return seed;
    }

private:
    template <class T>
    static void hash_combine(std::size_t& seed, const T& v) {
        std::hash<T> hasher;
        seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    }
};