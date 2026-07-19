#ifndef FOREST_SORTING_TEST_SUPPORT_ID_DISPATCH_ORACLE_HPP
#define FOREST_SORTING_TEST_SUPPORT_ID_DISPATCH_ORACLE_HPP

#include "forest_sorting/detail/id_chunks.hpp"
#include "forest_sorting/detail/id_compare.hpp"
#include "test_harness.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace forest_sorting::test_support {

enum class IdDispatchPath : std::uint8_t {
    Trait,
    Native,
    CachedChunk,
    MsbFallback,
    ByteFallback,
    Chunk1,
    Chunk2,
    Chunk4,
    Chunk8
};

struct IdDispatchCounters {
    static constexpr std::size_t pathCount = 9;

    std::array<std::size_t, pathCount> counts{};

    void reset() noexcept { counts.fill(0); }

    void record(IdDispatchPath path) noexcept {
        ++counts[static_cast<std::size_t>(path)];
    }

    [[nodiscard]] std::size_t count(IdDispatchPath path) const noexcept {
        return counts[static_cast<std::size_t>(path)];
    }

    [[nodiscard]] std::size_t total() const noexcept {
        std::size_t sum = 0;
        for (std::size_t value : counts) {
            sum += value;
        }
        return sum;
    }
};

inline void requireDispatchUsed(const IdDispatchCounters &counters,
                                IdDispatchPath path, const char *message) {
    require(counters.count(path) > 0, message);
}

inline void requireDispatchUnused(const IdDispatchCounters &counters,
                                  IdDispatchPath path, const char *message) {
    require(counters.count(path) == 0, message);
}

inline void requireNoDispatch(const IdDispatchCounters &counters,
                              const char *message) {
    require(counters.total() == 0, message);
}

struct InstrumentedTraitId {
    uint64_t value = 0;
};

struct InstrumentedTraitTraits {
    using Id = InstrumentedTraitId;
    static constexpr std::size_t id_byte_count = 8;

    IdDispatchCounters *counters = nullptr;

    bool less(const Id &lhs, const Id &rhs) const noexcept {
        record(IdDispatchPath::Trait);
        return lhs.value < rhs.value;
    }

    bool equal(const Id &lhs, const Id &rhs) const noexcept {
        record(IdDispatchPath::Trait);
        return lhs.value == rhs.value;
    }

    uint8_t byte_msb_first(const Id &nodeId,
                           std::size_t byteIndex) const noexcept {
        (void)nodeId;
        (void)byteIndex;
        record(IdDispatchPath::ByteFallback);
        return 0;
    }

  private:
    void record(IdDispatchPath path) const noexcept {
        if (counters != nullptr) {
            counters->record(path);
        }
    }
};

struct InstrumentedLessOnlyTraits {
    using Id = InstrumentedTraitId;
    static constexpr std::size_t id_byte_count = 8;

    IdDispatchCounters *counters = nullptr;

    bool less(const Id &lhs, const Id &rhs) const noexcept {
        record(IdDispatchPath::Trait);
        return lhs.value < rhs.value;
    }

    uint8_t byte_msb_first(const Id &nodeId,
                           std::size_t byteIndex) const noexcept {
        record(IdDispatchPath::ByteFallback);
        const std::size_t shift = (id_byte_count - 1U - byteIndex) * 8U;
        return static_cast<uint8_t>(nodeId.value >> shift);
    }

  private:
    void record(IdDispatchPath path) const noexcept {
        if (counters != nullptr) {
            counters->record(path);
        }
    }
};

struct InstrumentedNativeId {
    uint64_t value = 0;

    inline static IdDispatchCounters *counters = nullptr;

    bool operator<(const InstrumentedNativeId &other) const noexcept {
        record(IdDispatchPath::Native);
        return value > other.value;
    }

    bool operator==(const InstrumentedNativeId &other) const noexcept {
        record(IdDispatchPath::Native);
        return value != other.value;
    }

  private:
    static void record(IdDispatchPath path) noexcept {
        if (counters != nullptr) {
            counters->record(path);
        }
    }
};

struct InstrumentedNativeTraits {
    using Id = InstrumentedNativeId;
    static constexpr std::size_t id_byte_count = 8;

    static uint8_t byte_msb_first(const Id &nodeId,
                                  std::size_t byteIndex) noexcept {
        const std::size_t shift = (id_byte_count - 1U - byteIndex) * 8U;
        return static_cast<uint8_t>(nodeId.value >> shift);
    }
};

template <std::size_t ByteCount> struct InstrumentedByteId {
    std::array<uint8_t, ByteCount> bytes{};
};

template <std::size_t ByteCount> struct InstrumentedByteTraits {
    using Id = InstrumentedByteId<ByteCount>;
    static constexpr std::size_t id_byte_count = ByteCount;

    IdDispatchCounters *counters = nullptr;
    bool recordMsbFallback = false;

    uint8_t byte_msb_first(const Id &nodeId,
                           std::size_t byteIndex) const noexcept {
        record(IdDispatchPath::ByteFallback);
        recordFallback();
        return nodeId.bytes[byteIndex];
    }

    template <std::size_t ChunkBytes>
    forest_sorting::detail::ChunkValueType<ChunkBytes>
    chunk_msb_first(const Id &nodeId, std::size_t chunkIndex) const noexcept {
        static_assert(ChunkBytes == 1 || ChunkBytes == 2 || ChunkBytes == 4 ||
                      ChunkBytes == 8);
        record(chunkPath<ChunkBytes>());
        recordFallback();

        std::uint64_t value = 0;
        const std::size_t firstByte = chunkIndex * ChunkBytes;
        for (std::size_t offset = 0; offset < ChunkBytes; ++offset) {
            const std::size_t byteIndex = firstByte + offset;
            value <<= 8U;
            if (byteIndex < ByteCount) {
                value |= nodeId.bytes[byteIndex];
            }
        }
        return static_cast<forest_sorting::detail::ChunkValueType<ChunkBytes>>(
            value);
    }

  private:
    void record(IdDispatchPath path) const noexcept {
        if (counters != nullptr) {
            counters->record(path);
        }
    }

    void recordFallback() const noexcept {
        if (recordMsbFallback) {
            record(IdDispatchPath::MsbFallback);
        }
    }

    template <std::size_t ChunkBytes>
    static constexpr IdDispatchPath chunkPath() noexcept {
        if constexpr (ChunkBytes == 1) {
            return IdDispatchPath::Chunk1;
        } else if constexpr (ChunkBytes == 2) {
            return IdDispatchPath::Chunk2;
        } else if constexpr (ChunkBytes == 4) {
            return IdDispatchPath::Chunk4;
        } else {
            return IdDispatchPath::Chunk8;
        }
    }
};

template <std::size_t ChunkCount>
int compareCachedIdChunksWithOracle(
    const forest_sorting::detail::CachedChunkId<ChunkCount> &lhs,
    const forest_sorting::detail::CachedChunkId<ChunkCount> &rhs,
    IdDispatchCounters &counters) noexcept {
    counters.record(IdDispatchPath::CachedChunk);
    return forest_sorting::detail::compareCachedIdChunks(lhs, rhs);
}

} // namespace forest_sorting::test_support

#endif // FOREST_SORTING_TEST_SUPPORT_ID_DISPATCH_ORACLE_HPP
