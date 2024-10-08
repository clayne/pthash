#pragma once

#include "include/utils/bucketers.hpp"
#include "include/builders/util.hpp"
#include "include/builders/internal_memory_builder_single_phf.hpp"
#include "include/builders/external_memory_builder_single_phf.hpp"

namespace pthash {

template <typename Hasher, typename Encoder, bool Minimal>
struct single_phf {
    typedef Encoder encoder_type;
    static constexpr bool minimal = Minimal;

    template <typename Iterator>
    build_timings build_in_internal_memory(Iterator keys, uint64_t n,
                                           build_configuration const& config) {
        internal_memory_builder_single_phf<Hasher> builder;
        auto timings = builder.build_from_keys(keys, n, config);
        timings.encoding_seconds = build(builder, config);
        return timings;
    }

    template <typename Iterator>
    build_timings build_in_external_memory(Iterator keys, uint64_t n,
                                           build_configuration const& config) {
        external_memory_builder_single_phf<Hasher> builder;
        auto timings = builder.build_from_keys(keys, n, config);
        timings.encoding_seconds = build(builder, config);
        return timings;
    }

    template <typename Builder>
    double build(Builder const& builder, build_configuration const& config) {
        auto start = clock_type::now();
        if (Minimal && !config.minimal_output) {
            throw std::runtime_error(
                "Cannot build single_phf<..., ..., true> with minimal_output=false");
        } else if (!Minimal && config.minimal_output) {
            throw std::runtime_error(
                "Cannot build single_phf<..., ..., false> with minimal_output=true");
        }
        m_seed = builder.seed();
        m_num_keys = builder.num_keys();
        m_table_size = builder.table_size();
        m_M = fastmod::computeM_u64(m_table_size);
        m_bucketer = builder.bucketer();
        m_pilots.encode(builder.pilots().data(), m_bucketer.num_buckets());
        if (Minimal and m_num_keys < m_table_size) {
            m_free_slots.encode(builder.free_slots().begin(), m_table_size - m_num_keys);
        }
        auto stop = clock_type::now();
        return seconds(stop - start);
    }

    template <typename T>
    uint64_t operator()(T const& key) const {
        auto hash = Hasher::hash(key, m_seed);
        return position(hash);
    }

    uint64_t position(typename Hasher::hash_type hash) const {
        uint64_t bucket = m_bucketer.bucket(hash.first());
        uint64_t pilot = m_pilots.access(bucket);
        uint64_t hashed_pilot = default_hash64(pilot, m_seed);
        uint64_t p = fastmod::fastmod_u64(hash.second() ^ hashed_pilot, m_M, m_table_size);
        if constexpr (Minimal) {
            if (PTHASH_LIKELY(p < num_keys())) return p;
            return m_free_slots.access(p - num_keys());
        }
        return p;
    }

    uint64_t num_bits_for_pilots() const {
        return 8 * (sizeof(m_seed) + sizeof(m_num_keys) + sizeof(m_table_size) + sizeof(m_M)) +
               m_bucketer.num_bits() + m_pilots.num_bits();
    }

    uint64_t num_bits_for_mapper() const {
        return m_free_slots.num_bytes() * 8;
    }

    uint64_t num_bits() const {
        return num_bits_for_pilots() + num_bits_for_mapper();
    }

    inline uint64_t num_keys() const {
        return m_num_keys;
    }

    inline uint64_t table_size() const {
        return m_table_size;
    }

    inline uint64_t seed() const {
        return m_seed;
    }

    template <typename Visitor>
    void visit(Visitor& visitor) const {
        visit_impl(visitor, *this);
    }

    template <typename Visitor>
    void visit(Visitor& visitor) {
        visit_impl(visitor, *this);
    }

private:
    template <typename Visitor, typename T>
    static void visit_impl(Visitor& visitor, T&& t) {
        visitor.visit(t.m_seed);
        visitor.visit(t.m_num_keys);
        visitor.visit(t.m_table_size);
        visitor.visit(t.m_M);
        visitor.visit(t.m_bucketer);
        visitor.visit(t.m_pilots);
        visitor.visit(t.m_free_slots);
    }
    uint64_t m_seed;
    uint64_t m_num_keys;
    uint64_t m_table_size;
    __uint128_t m_M;
    skew_bucketer m_bucketer;
    Encoder m_pilots;
    bits::elias_fano<false, false> m_free_slots;
};

}  // namespace pthash