#include "topk_baselines.h"
#include "topk_validation.h"

#include <iostream>

int main() {
    bool all_passed = true;

    if (!run_validation(topk_minheap, "min-heap")) {
        all_passed = false;
    }

    if (!run_validation(topk_nth_element, "nth_element")) {
        all_passed = false;
    }

    if (all_passed) {
        std::cout << "VALIDATION PASSED\n";
    } else {
        std::cout << "VALIDATION FAILED\n";
    }

    return all_passed ? 0 : 1;
}
