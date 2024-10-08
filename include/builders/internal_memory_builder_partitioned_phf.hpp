#pragma once

#include "include/builders/util.hpp"
#include "include/builders/internal_memory_builder_single_phf.hpp"

namespace pthash {

template <typename Hasher>
struct internal_memory_builder_partitioned_phf {
    typedef Hasher hasher_type;

    template <typename Iterator>
    build_timings build_from_keys(Iterator keys, uint64_t num_keys,
                                  build_configuration const& config) {
        build_configuration actual_config = config;
        if (config.seed == constants::invalid_seed) actual_config.seed = random_value();
        return build_from_hashes(hash_generator<Iterator, hasher_type>(keys, actual_config.seed),
                                 num_keys, actual_config);
    }

    template <typename Iterator>
    build_timings build_from_hashes(Iterator hashes, uint64_t num_keys,
                                    build_configuration const& config) {
        assert(num_keys > 0);
        util::check_hash_collision_probability<Hasher>(num_keys);

        if (config.num_partitions == 0) {
            throw std::invalid_argument("number of partitions must be > 0");
        }

        auto start = clock_type::now();

        build_timings timings;

        uint64_t num_partitions = config.num_partitions;
        double average_partition_size = static_cast<double>(num_keys) / num_partitions;
        if (average_partition_size < constants::min_partition_size and num_partitions > 1) {
            num_partitions = 1;
            average_partition_size = 1.0;
        }

        if (config.verbose_output) std::cout << "num_partitions " << num_partitions << std::endl;

        m_seed = config.seed;
        m_num_keys = num_keys;
        m_table_size = 0;
        m_num_partitions = num_partitions;
        m_bucketer.init(num_partitions);
        m_offsets.resize(num_partitions);
        m_builders.resize(num_partitions);

        std::vector<std::vector<typename hasher_type::hash_type>> partitions(num_partitions);
        for (auto& partition : partitions) partition.reserve(1.5 * average_partition_size);

        progress_logger logger(num_keys, " == partitioned ", " keys", config.verbose_output);
        for (uint64_t i = 0; i != num_keys; ++i, ++hashes) {
            auto hash = *hashes;
            auto b = m_bucketer.bucket(hash.mix());
            partitions[b].push_back(hash);
            logger.log();
        }
        logger.finalize();

        for (uint64_t i = 0, cumulative_size = 0; i != num_partitions; ++i) {
            auto const& partition = partitions[i];
            uint64_t table_size = static_cast<double>(partition.size()) / config.alpha;
            if ((table_size & (table_size - 1)) == 0) table_size += 1;
            m_table_size += table_size;
            m_offsets[i] = cumulative_size;
            cumulative_size += config.minimal_output ? partition.size() : table_size;
        }

        auto partition_config = config;
        partition_config.num_partitions = num_partitions;
        partition_config.seed = m_seed;
        const uint64_t num_buckets_single_phf =
            std::ceil((config.c * num_keys) / (num_keys > 1 ? std::log2(num_keys) : 1));
        partition_config.num_buckets = static_cast<double>(num_buckets_single_phf) / num_partitions;
        partition_config.verbose_output = false;
        partition_config.num_threads = 1;

        timings.partitioning_seconds = seconds(clock_type::now() - start);

        auto t = build_partitions(partitions.begin(), m_builders.begin(), partition_config,
                                  config.num_threads);
        timings.mapping_ordering_seconds = t.mapping_ordering_seconds;
        timings.searching_seconds = t.searching_seconds;

        return timings;
    }

    template <typename PartitionsIterator, typename BuildersIterator>
    static build_timings build_partitions(PartitionsIterator partitions, BuildersIterator builders,
                                          build_configuration const& config, uint64_t num_threads) {
        build_timings timings;
        uint64_t num_partitions = config.num_partitions;
        assert(config.num_threads == 1);

        if (num_threads > 1) {  // parallel
            std::vector<std::thread> threads(num_threads);
            std::vector<build_timings> thread_timings(num_threads);

            auto exe = [&](uint64_t i, uint64_t begin, uint64_t end) {
                for (; begin != end; ++begin) {
                    auto const& partition = partitions[begin];
                    auto t = builders[begin].build_from_hashes(partition.begin(), partition.size(),
                                                               config);
                    thread_timings[i].mapping_ordering_seconds += t.mapping_ordering_seconds;
                    thread_timings[i].searching_seconds += t.searching_seconds;
                }
            };

            uint64_t num_partitions_per_thread = (num_partitions + num_threads - 1) / num_threads;
            for (uint64_t i = 0, begin = 0; i != num_threads; ++i) {
                uint64_t end = begin + num_partitions_per_thread;
                if (end > num_partitions) end = num_partitions;
                threads[i] = std::thread(exe, i, begin, end);
                begin = end;
            }

            for (auto& t : threads) {
                if (t.joinable()) t.join();
            }

            for (auto const& t : thread_timings) {
                if (t.mapping_ordering_seconds > timings.mapping_ordering_seconds)
                    timings.mapping_ordering_seconds = t.mapping_ordering_seconds;
                if (t.searching_seconds > timings.searching_seconds)
                    timings.searching_seconds = t.searching_seconds;
            }
        } else {  // sequential
            for (uint64_t i = 0; i != num_partitions; ++i) {
                auto const& partition = partitions[i];
                auto t = builders[i].build_from_hashes(partition.begin(), partition.size(), config);
                timings.mapping_ordering_seconds += t.mapping_ordering_seconds;
                timings.searching_seconds += t.searching_seconds;
            }
        }
        return timings;
    }

    uint64_t seed() const {
        return m_seed;
    }

    uint64_t num_keys() const {
        return m_num_keys;
    }

    uint64_t table_size() const {
        return m_table_size;
    }

    uint64_t num_partitions() const {
        return m_num_partitions;
    }

    uniform_bucketer bucketer() const {
        return m_bucketer;
    }

    std::vector<uint64_t> const& offsets() const {
        return m_offsets;
    }

    std::vector<internal_memory_builder_single_phf<hasher_type>> const& builders() const {
        return m_builders;
    }

private:
    uint64_t m_seed;
    uint64_t m_num_keys;
    uint64_t m_table_size;
    uint64_t m_num_partitions;
    uniform_bucketer m_bucketer;
    std::vector<uint64_t> m_offsets;
    std::vector<internal_memory_builder_single_phf<hasher_type>> m_builders;
};

}  // namespace pthash