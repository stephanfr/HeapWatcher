#pragma once

#include <chrono>
#include <future>
#include <iostream>
#include <random>
#include <thread>
#include <vector>

#include "HeapWatcher.hpp"


namespace SEFUtils::HeapWatcher
{
    using namespace std::literals::chrono_literals;

    class MultithreadedTestFixture
    {
       public:
        static constexpr size_t MAX_NUM_TEST_THREADS = 100;

        MultithreadedTestFixture() : start_synchronizer_future_(start_synchronizer_.get_future())
        {
            test_threads_.reserve(MAX_NUM_TEST_THREADS);
        }

        void add_workload(size_t num_workers, const std::function<void()>& workload_function, double random_start_in_seconds = 0)
        {
            std::random_device rd;  //Will be used to obtain a seed for the random number engine
            std::mt19937 gen(rd()); //Standard mersenne_twister_engine seeded with rd()
            std::uniform_real_distribution<double> distrib(0, random_start_in_seconds);


            for (int i = 0; i < num_workers; i++)
            {
                if( random_start_in_seconds <= 0 )
                {
                test_threads_.emplace_back(
                    std::thread(&MultithreadedTestFixture::workload_wrapper, this, workload_function, 0s));
                }
                else
                {
                test_threads_.emplace_back(
                    std::thread(&MultithreadedTestFixture::workload_wrapper, this, workload_function, std::chrono::duration<double>(distrib(gen))));
                }
            }
        }

        void start_workload()
        {
            SEFUtils::HeapWatcher::get_heap_watcher().start_watching();

            start_synchronizer_.set_value();
        }

        HeapSnapshot wait_for_completion()
        {
            for ( auto& current_thread : test_threads_)
            {
                current_thread.join();
            }

            return SEFUtils::HeapWatcher::get_heap_watcher().stop_watching();
        }

       private:
        std::vector<std::thread> test_threads_;
        std::promise<void> start_synchronizer_;
        std::shared_future<void> start_synchronizer_future_;

        void workload_wrapper(const std::function<void()>& workload_function, std::chrono::duration<double>  random_start)
        {
            std::shared_future<void> synchronizer(start_synchronizer_future_);

            synchronizer.wait();

            if( random_start > 0s )
            {
                std::this_thread::sleep_for(random_start);
            }

            workload_function();
        }
    };
}  // namespace SEFUtils::HeapWatcher
