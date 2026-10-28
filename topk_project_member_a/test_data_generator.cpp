#include "data_generator.h"
#include <iostream>
#include <vector>

void print_data(const std::vector<float>& data) {
    for (float value : data) {
        std::cout << value << " ";
    }
    std::cout << "\n";
}

int main() {
    const size_t N = 10;

    std::cout << "uniform: ";
    print_data(generate_uniform(N));

    std::cout << "normal: ";
    print_data(generate_normal(N));

    std::cout << "descending: ";
    print_data(generate_descending(N));

    std::cout << "duplicates: ";
    print_data(generate_duplicates(N));

    std::cout << "boundary values: ";
    print_data(generate_boundary_values(N));

    return 0;
}