obj-$(CONFIG_ION_MAINLINE) +=	ion.o ion_heap.o ion_page_pool.o ion_system_heap.o \
			ion_carveout_heap.o ion_chunk_heap.o ion_cma_heap.o
obj-$(CONFIG_ION_MAINLINE_TEST) += ion_test.o
ifdef CONFIG_COMPAT
obj-$(CONFIG_ION_MAINLINE) += compat_ion.o
endif
obj-$(CONFIG_ION_MAINLINE_TEGRA) += tegra/
