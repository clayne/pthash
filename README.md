PTHash
------

PTHash is a C++ library implementing fast and compact minimal perfect hash functions as described in the paper
[*PTHash: Revisiting FCH Minimal Perfect Hashing*]() [1].

Given a set *S* of *n* distinct keys, a function *f* that bijectively maps the keys of *S* into the first *n* natural numbers
is called a *minimal perfect hash function* (MPHF) for *S*.
Algorithms that find such functions when *n* is large and retain constant evaluation time are of practical interest.
For instance, search engines and databases typically use minimal perfect hash functions to quickly assign identifiers to static sets of variable-length keys such as strings.
The challenge is to design an algorithm which is efficient in three different aspects: time to find *f* (construction time), time to evaluate *f* on a key of *S* (lookup time), and space of representation for *f*.

PTHash is one such algorithm [1].

The following guide is meant to provide a brief overview of the library
by illustrating its functionalities through some examples.
Scripts to generate the data for the tables used in the paper
are also provided.

##### Table of contents
* [Integration](#integration)
* [Compiling the Code](#compiling-the-code)
* [Quick Start](#quick-start)
* [Build Examples](#build-examples)
* [An Example Benchmark](#an-example-benchmark)
* [Authors](#authors)
* [References](#references)

Integration
-----
Integrating PTHash in your own project is very simple: just get the source code
and include the header `include/pthash.hpp` in your code.
No other configurations are needed.

If you use `git`, the easiest way to add PTHash is via `git add submodule` as follows.

	git submodule add https://github.com/jermp/pthash.git

Compiling the Code
-----

The code is tested on Linux with `gcc` and on Mac with `clang`.
To build the code, [`CMake`](https://cmake.org/) is required.

Clone the repository with

	git clone --recursive https://github.com/jermp/pthash.git

If you have cloned the repository without `--recursive`, you will need to perform the following commands before
compiling:

    git submodule init
    git submodule update

To compile the code for a release environment (see file `CMakeLists.txt` for the used compilation flags), it is sufficient to do the following:

    mkdir build
    cd build
    cmake ..
    make -j

For a testing environment, use the following instead:

    mkdir debug_build
    cd debug_build
    cmake .. -DCMAKE_BUILD_TYPE=Debug -DUSE_SANITIZERS=On
    make

(NOTE: Beware that the software will result in a much slower execution when running in debug mode and using sanitizers. Use this only for debug purposes, not to run performance tests.)

Quick Start
-----

For a quick start, see the source file `src/example.cpp` (reported below).
The example shows how to setup a simple build configuration
for PTHash (parameters, bash hasher, and encoder).

After compilation, run this example with

	./example

which will build a PTHash MPHF on 10M random 64-bit keys
using c = 7.0 and alpha = 0.94.
It also shows how to serialize the data structure on disk
and re-load it for evaluation.


```C++
#include <iostream>
#include "../include/pthash.hpp"
#include "util.hpp"  // for functions distinct_keys and check

int main() {
    using namespace pthash;

    /* Generate 10M random 64-bit keys as input data. */
    static const uint64_t num_keys = 10000000;
    static const uint64_t seed = 1234567890;
    std::cout << "generating input data..." << std::endl;
    std::vector<uint64_t> keys = distinct_keys<uint64_t>(num_keys, seed);
    assert(keys.size() == num_keys);

    /* Set up a build configuration. */
    build_configuration config;
    config.c = 6.0;
    config.alpha = 0.94;
    config.verbose_output = true;

    /* Declare the PTHash function. */
    typedef single_mphf<murmurhash2_64,        // base hasher
                        dictionary_dictionary  // encoder type
                        >
        pthash_type;
    pthash_type f;

    /* Build the function in internal memory. */
    std::cout << "building the MPHF..." << std::endl;
    f.build_in_internal_memory(keys.begin(), keys.size(), config);

    /* Compute and print the number of bits spent per key. */
    double bits_per_key = static_cast<double>(f.num_bits()) / f.num_keys();
    std::cout << "MPHF uses " << bits_per_key << " [bits/key]" << std::endl;

    /* Sanity check! */
    if (check(keys.begin(), keys.size(), f)) std::cout << "EVERYTHING OK!" << std::endl;

    /* Now evaluate f on some keys. */
    for (uint64_t i = 0; i != 10; ++i) {
        std::cout << "f(" << keys[i] << ") = " << f(keys[i]) << '\n';
    }

    /* Serialize the data structure to a file. */
    std::cout << "serializing the MPHF to disk..." << std::endl;
    std::string output_filename("mphf.bin");
    essentials::save(f, output_filename.c_str());

    {
        /* Now reload from disk and query. */
        pthash_type other;
        essentials::load(other, output_filename.c_str());
        for (uint64_t i = 0; i != 10; ++i) {
            std::cout << "f(" << keys[i] << ") = " << other(keys[i]) << '\n';
            assert(f(keys[i]) == other(keys[i]));
        }
    }

    return 0;
}
```

Build Examples
-----

All the examples below must be run from within the directory
where the code was compiled (see the section [Compiling the Code](#compiling-the-code)), using the driver program
called `build`.

Running the command

	./build --help

shows the usage of the driver program, as reported below.

	Usage: ./build [-h,--help] num_keys c alpha encoder_type [-p num_partitions] [-s seed] [-t num_threads] [-i input_filename] [-o output_filename] [-d tmp_dir] [--external] [--verbose] [--check] [--lookup]

	 num_keys
		The size of the input.
	 c
		A constant that trades construction speed for space effectiveness.
	 alpha
		The table load factor. It must be a quantity > 0 and <= 1.
	 encoder_type
		The encoder type. See include/encoders/encoders.hpp for a list of available types.
	 [-p num_partitions]
		Number of partitions.
	 [-s seed]
		Seed to use for construction.
	 [-t num_threads]
		Number of threads to use for construction.
	 [-i input_filename]
		A string input file name. If this is not provided, then num_keys 64-bit random keys will be used as input instead.
	 [-o output_filename]
		Output file name where the MPHF will be serialized.
	 [-d tmp_dir]
		Temporary directory used for building in external memory. Default is directory '.'.
	 [--external]
		Build the MPHF in external memory.
	 [--verbose]
		Verbose output during construction.
	 [--check]
		Check correctness after construction.
	 [--lookup]
		Measure average lookup time after construction.
	 [-h,--help]
		Print this help text and silently exits.

#### Example 1

	./build 1000000 4.5 0.99 dictionary_dictionary -s 727369 --verbose --check --lookup -o mphf.bin

This example will build a MPHF over 1M random 64-bit keys (generated with seed 727369), using c = 4.5, alpha = 0.99, and compressing the MPHF data structure with the encoder `dictionary_dictionary`.

The data structure will be serialized on a binary file named `mphf.bin`.

It will also check the correctness of the data structure (flag `--check`) and measure average lookup time (flag `--lookup`).

Construction will happen in **internal memory**, using a **single processing thread**.
(Experimental setting of the paper.)


#### Example 2

For the following example,
we are going to use the strings from the UK-2005 URLs collection,
which can be downloaded by clicking
[here](http://data.law.di.unimi.it/webdata/uk-2005/uk-2005.urls.gz).
(This is also one of the datasets used in the paper.)

The file is ~300 MB compressed using gzip (2.86 GB uncompressed).

After download, place the dataset in the `build` directory and run

	gunzip uk-2005.urls.gz

to uncompress it.
The file contains one string per line, for a total of 39,459,925 strings.

#### NOTE: Input files are read line by line (i.e., individual strings are assumed to be separated by the character `\n`).

The following command will build a MPHF using the strings of the file as input keys,
with c = 7.0, alpha = 0.94.

	./build 39459925 7.0 0.94 dictionary_dictionary -s 1234567890 -i uk-2005.urls --verbose --check --lookup


#### Example 3

	./build 39459925 7.0 0.94 dictionary_dictionary -s 1234567890 -i uk-2005.urls --verbose --check --lookup -p 128 -t 4

This example will run the construction over the same input and parameters used in Example 2,
but with 128 partitions and using **4 parallel threads**.
The resulting data structure will consume essentially the same space as that built in Example 2 and only slightly slower at lookup.


#### Example 4

	./build 39459925 7.0 0.94 dictionary_dictionary -s 1234567890 -i uk-2005.urls --verbose --check --lookup --external

This example will run the construction over the same input and parameters used in Example 2,
but using **external memory**.
The resulting data structure will be exactly the same as that built in Example 2.


An Example Benchmark
-----

The script `script/run_benchmark.sh` runs the 4 trade-off configurations (encoder, alpha, c) described in Section 5.2 of the paper [1] on 100M and 1000M keys.

C-C stands for "compact-compact" encoder; D-D for "dictionary-dictionary"; and EF for "Elias-Fano".

From within the directory where the code has been compiled, just run

	bash ../script/run_benchmark.sh 2> results.json

to reproduce the bottom part of Table 5 of the SIGIR 2021 paper [1].

Below, the result of the benchmark across different processors and compilers.
The code is compiled with `-O3` and `-march=native` in all cases.

#### Intel i9-9900K @ 3.60 GHz, gcc 9.2.1, GNU/Linux 5.4.0-70-generic x86_64

| Configuration |100M keys ||| 1000M keys |||
|:---------------|:---:|:---:|:---:|:---:|:---:|:---:|
|                |constr. (sec) | space (bits/key) | lookup (ns/key) | constr. (sec) | space (bits/key) | lookup (ns/key) |
| (1) C-C, alpha = 0.99, c = 7.0 | 42 | 3.36 | 28 | 1042 | 3.23 | 37 |
| (2) D-D, alpha = 0.88, c = 11.0 | 19 | 4.05 | 46 | 308 | 3.94 | 64 |
| (3) EF, alpha = 0.99, c = 6.0 | 45 | 2.26 | 49 | 1799 | 2.17 | 101 |
| (4) D-D, alpha = 0.94, c = 7.0 | 26 | 3.23 | 37 | 689 | 2.99 | 55 |


#### Intel i7-7700 @ 3.60 GHz, gcc 9.3.0, GNU/Linux 5.4.0-66-generic x86_64

| Configuration  |100M keys ||| 1000M keys |||
|:---------------|:---:|:---:|:---:|:---:|:---:|:---:|
|                |constr. (sec) | space (bits/key) | lookup (ns/key) | constr. (sec) | space (bits/key) | lookup (ns/key) |
| (1) C-C, alpha = 0.99, c = 7.0 | 59 | 3.36 | 35 | 1145 | 3.23 | 40 |
| (2) D-D, alpha = 0.88, c = 11.0 | 27 | 4.05 | 57 | 357 | 3.94 | 69 |
| (3) EF, alpha = 0.99, c = 6.0 | 86 | 2.26 | 66 | 1918 | 2.17 | 110 |
| (4) D-D, alpha = 0.94, c = 7.0 | 45 | 3.23 | 48 | 796 | 2.99 | 61 |


#### Intel i7-4790K @ 4.00GHz, gcc 8.3.0, GNU/Linux 5.0.0-27-generic x86_64

| Configuration  |100M keys ||| 1000M keys |||
|:---------------|:---:|:---:|:---:|:---:|:---:|:---:|
|                |constr. (sec) | space (bits/key) | lookup (ns/key) | constr. (sec) | space (bits/key) | lookup (ns/key) |
| (1) C-C, alpha = 0.99, c = 7.0 | 55 | 3.36 | 41 | 1156 | 3.23 | 51 |
| (2) D-D, alpha = 0.88, c = 11.0 | 26 | 4.05 | 55 | 422 | 3.94 | 69 |
| (3) EF, alpha = 0.99, c = 6.0 | 81 | 2.26 | 69 | 1921 | 2.17 | 147 |
| (4) D-D, alpha = 0.94, c = 7.0 | 42 | 3.23 | 47 | 812 | 2.99 | 60 |

Authors
-----
* [Giulio Ermanno Pibiri](http://pages.di.unipi.it/pibiri/), <giulio.ermanno.pibiri@isti.cnr.it>
* [Roberto Trani](), <roberto.trani@isti.cnr.it>

References
-----
* [1] Giulio Ermanno Pibiri and Roberto Trani. *"PTHash: Revisiting FCH Minimal Perfect Hashing"*. In Proceedings of the 44th International
Conference on Research and Development in Information Retrieval (SIGIR). 2021.