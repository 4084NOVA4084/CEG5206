SOURCES = topk_minheap.cpp topk_nth_element.cpp topk_oracle.cpp data_generator.cpp \
          topk_validation.cpp validate_topk.cpp

validate_topk: $(SOURCES)
	g++ -std=c++17 -O2 $(SOURCES) -o validate_topk

clean:
ifeq ($(OS),Windows_NT)
	del validate_topk.exe
else
	rm -f validate_topk
endif

.PHONY: clean
