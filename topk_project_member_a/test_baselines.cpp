#include "topk_baselines.h"
#include "topk_oracle.h"

#include <iostream>
#include <vector>

void print_result(const std::vector<float>& result) {
    for (float value : result) {
        std::cout << value << " ";
    }
    std::cout << "\n";
}

int main() {
    std::vector<float> input = {3.2f, 8.5f, 1.1f, 6.7f, 9.3f, 2.4f, 7.8f, 4.1f, 5.6f, 0.9f};

    size_t N = input.size();
    size_t K = 3;

    std::vector<float> minheap_output(K);
    std::vector<float> nth_output(K);
    std::vector<float> oracle_output(K);

    topk_minheap(input.data(), N, K, minheap_output.data());

    topk_nth_element(input.data(), N, K, nth_output.data());

    topk_oracle(input.data(), N, K, oracle_output.data());

    std::cout << "Min-Heap Top-K: ";
    print_result(minheap_output);

    std::cout << "nth_element Top-K: ";
    print_result(nth_output);

    std::cout << "Oracle Top-K: ";
    print_result(oracle_output);

    return 0;
}