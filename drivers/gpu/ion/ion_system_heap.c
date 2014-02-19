/*
 * drivers/gpu/ion/ion_system_heap.c
 *
 * Copyright (C) 2011 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <asm/page.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/highmem.h>
#include <linux/ion.h>
#include <linux/mm.h>
#include <linux/scatterlist.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <asm/tlbflush.h>
#include "ion_priv.h"

static unsigned int high_order_gfp_flags = (GFP_HIGHUSER | __GFP_NOWARN |
						__GFP_NORETRY) & ~__GFP_WAIT;
static unsigned int low_order_gfp_flags  = (GFP_HIGHUSER | __GFP_NOWARN);
static const unsigned int orders[] = {8, 4, 0};
static const int num_orders = ARRAY_SIZE(orders);
static int order_to_index(unsigned int order)
{
	int i;
	for (i = 0; i < num_orders; i++)
		if (order == orders[i])
			return i;
	BUG();
	return -1;
}

static unsigned int order_to_size(int order)
{
	return PAGE_SIZE << order;
}

#define VM_PAGE_COUNT_WIDTH 4	/* 8 slots stack */
#define VM_PAGE_COUNT 4		/* value range 0 ~ 3 */
struct ion_system_heap {
	struct ion_heap heap;
	struct ion_page_pool **pools;
	struct semaphore vm_sem;
	atomic_t page_idx; /* Max. value is the count of vm_sem */
	struct vm_struct *reserved_vm_area; /* PAGE_SIZE * VM_PAGE_COUNT*/
	pte_t **pte; /* pte for reserved_vm_area */
};

struct page_info {
	struct page *page;
	unsigned int order;
	struct list_head list;
	bool from_pool;
};

static struct page *alloc_buffer_page(struct ion_system_heap *heap,
				      struct ion_buffer *buffer,
				      unsigned long order,
				      bool *from_pool)
{
	int idx = order_to_index(order);
	struct ion_page_pool *pool;
	struct page *page;

	if (ion_buffer_fault_user_mappings(buffer)) {
		gfp_t gfp_flags = low_order_gfp_flags;

		if (order > 4)
			gfp_flags = high_order_gfp_flags;
		page = ion_heap_alloc_pages(buffer, gfp_flags, order);

		return page;
	}

	if (!ion_buffer_cached(buffer))
		idx += num_orders;

	pool = heap->pools[idx];

	page = ion_page_pool_alloc(pool, false, from_pool);
	if (!page) {
		/* try with alternative pool */
		if (ion_buffer_cached(buffer))
			pool = heap->pools[idx + num_orders];
		else
			pool = heap->pools[idx - num_orders];

		page = ion_page_pool_alloc(pool, true, from_pool);
		/*
		 * allocation from cached pool for nocached page allocation
		 * request must be treated as 'not from pool' because it
		 * requires cache flush.
		 */
		if (!ion_buffer_cached(buffer))
			*from_pool = false;
	}

	return page;
}

static void free_buffer_page(struct ion_system_heap *heap,
			     struct ion_buffer *buffer, struct page *page,
			     unsigned int order)
{
	int uncached = ion_buffer_cached(buffer) ? 0 : 1;
	int idx = order_to_index(order);
	bool split_pages = ion_buffer_fault_user_mappings(buffer);
	int i;
	struct ion_page_pool *pool = heap->pools[idx + num_orders * uncached];

	if (split_pages) {
		for (i = 0; i < (1 << order); i++)
			__free_page(page + i);
	} else if (!!(buffer->flags & ION_FLAG_SHRINKER_FREE)) {
		__free_pages(page, order);
	} else {
		ion_page_pool_free(pool, page);
	}
}


static struct page_info *alloc_largest_available(struct ion_system_heap *heap,
						 struct ion_buffer *buffer,
						 unsigned long size,
						 unsigned int max_order)
{
	struct page *page;
	struct page_info *info;
	int i;
	bool from_pool = false;

	for (i = 0; i < num_orders; i++) {
		if (size < order_to_size(orders[i]))
			continue;
		if (max_order < orders[i])
			continue;

		page = alloc_buffer_page(heap, buffer, orders[i], &from_pool);
		if (!page)
			continue;

		info = kmalloc(sizeof(struct page_info), GFP_KERNEL);
		if (!info)
			return NULL;
		info->page = page;
		info->order = orders[i];
		info->from_pool = from_pool;
		return info;
	}
	return NULL;
}

#ifndef CONFIG_ION_EXYNOS
static void ion_clean_and_unmap(unsigned long vaddr, pte_t *ptep,
				size_t size, bool memory_zero)
{
	int i;

	flush_cache_vmap(vaddr, vaddr + size);

	if (memory_zero)
		memset((void *)vaddr, 0, size);

	dmac_flush_range((void *)vaddr, (void *)vaddr + size);

	for (i = 0; i < (size / PAGE_SIZE); i++)
		pte_clear(&init_mm, (void *)vaddr + (i * PAGE_SIZE), ptep + i);

	flush_cache_vunmap(vaddr, vaddr + size);
	flush_tlb_kernel_range(vaddr, vaddr + size);
}

static void ion_clean_and_init_allocated_pages(
		struct ion_system_heap *heap, struct scatterlist *sgl,
		int nents, bool memory_zero)
{
	int i;
	struct scatterlist *sg;
	size_t sum = 0;
	int page_idx;
	unsigned long vaddr;
	pte_t *ptep;

	down(&heap->vm_sem);

	page_idx = atomic_pop(&heap->page_idx, VM_PAGE_COUNT_WIDTH);
	BUG_ON((page_idx < 0) || (page_idx >= VM_PAGE_COUNT));

	ptep = heap->pte[page_idx * (SZ_1M / PAGE_SIZE)];
	vaddr = (unsigned long)heap->reserved_vm_area->addr +
				(SZ_1M * page_idx);

	for_each_sg(sgl, sg, nents, i) {
		int j;

		if (!PageHighMem(sg_page(sg))) {
			memset(page_address(sg_page(sg)), 0, sg_dma_len(sg));
			continue;
		}

		for (j = 0; j < (sg_dma_len(sg) / PAGE_SIZE); j++) {
			set_pte_at(&init_mm, vaddr, ptep,
					mk_pte(sg_page(sg) + j, PAGE_KERNEL));
			ptep++;
			vaddr += PAGE_SIZE;

		}

		sum += j * PAGE_SIZE;
		if (sum == SZ_1M) {
			ptep = heap->pte[page_idx * (SZ_1M / PAGE_SIZE)];
			vaddr = (unsigned long)heap->reserved_vm_area->addr +
						(SZ_1M * page_idx);

			ion_clean_and_unmap(vaddr, ptep, sum, memory_zero);

			sum = 0;
		}
	}

	if (sum != 0) {
		ion_clean_and_unmap(
			(unsigned long)heap->reserved_vm_area->addr +
				(SZ_1M * page_idx),
			heap->pte[page_idx * (SZ_1M / PAGE_SIZE)],
			sum, memory_zero);
	}

	atomic_push(&heap->page_idx, page_idx, VM_PAGE_COUNT_WIDTH);

	up(&heap->vm_sem);
}
#else
static void ion_clean_and_init_allocated_pages(
		struct ion_system_heap *heap, struct scatterlist *sgl,
		int nents, bool memory_zero)
{
}
#endif

static int ion_system_heap_allocate(struct ion_heap *heap,
				     struct ion_buffer *buffer,
				     unsigned long size, unsigned long align,
				     unsigned long flags)
{
	struct ion_system_heap *sys_heap = container_of(heap,
							struct ion_system_heap,
							heap);
	struct sg_table *table;
	struct scatterlist *sg;
	int ret;
	struct list_head pages;
	struct page_info *info, *tmp_info;
	int i = 0;
	unsigned long size_remaining = PAGE_ALIGN(size);
	unsigned int max_order = orders[0];
	bool all_pages_from_pool = true;

	INIT_LIST_HEAD(&pages);
	while (size_remaining > 0) {
		info = alloc_largest_available(sys_heap, buffer, size_remaining, max_order);
		if (!info)
			goto err;
		list_add_tail(&info->list, &pages);
		size_remaining -= (1 << info->order) * PAGE_SIZE;
		max_order = info->order;
		i++;
	}

	table = kmalloc(sizeof(struct sg_table), GFP_KERNEL);
	if (!table)
		goto err;

	ret = sg_alloc_table(table, i, GFP_KERNEL);
	if (ret)
		goto err1;

	sg = table->sgl;

	list_for_each_entry_safe(info, tmp_info, &pages, list) {
		struct page *page = info->page;
		sg_set_page(sg, page, (1 << info->order) * PAGE_SIZE, 0);
		sg = sg_next(sg);
		if (all_pages_from_pool && !info->from_pool)
			all_pages_from_pool = false;
		list_del(&info->list);
		kfree(info);
	}

	if (all_pages_from_pool)
		ion_buffer_set_ready(buffer);

	ion_clean_and_init_allocated_pages(
			sys_heap, table->sgl, table->orig_nents,
			!(flags & ION_FLAG_NOZEROED));

	buffer->priv_virt = table;
	return 0;
err1:
	kfree(table);
err:
	list_for_each_entry(info, &pages, list) {
		free_buffer_page(sys_heap, buffer, info->page, info->order);
		kfree(info);
	}
	return -ENOMEM;
}

void ion_system_heap_free(struct ion_buffer *buffer) {
	struct ion_heap *heap = buffer->heap;
	struct ion_system_heap *sys_heap = container_of(heap,
							struct ion_system_heap,
							heap);
	struct sg_table *table = buffer->sg_table;
	struct scatterlist *sg;
	int i;

	/* pages come from the page pools, zero them before returning
	   for security purposes (other allocations are zerod at alloc time */
	if (!ion_buffer_fault_user_mappings(buffer))
		ion_heap_buffer_zero(buffer);

	for_each_sg(table->sgl, sg, table->nents, i)
		free_buffer_page(sys_heap, buffer, sg_page(sg),
				get_order(sg_dma_len(sg)));
	sg_free_table(table);
	kfree(table);
}

struct sg_table *ion_system_heap_map_dma(struct ion_heap *heap,
					 struct ion_buffer *buffer)
{
	return buffer->priv_virt;
}

void ion_system_heap_unmap_dma(struct ion_heap *heap,
			       struct ion_buffer *buffer)
{
	return;
}

static struct ion_heap_ops system_heap_ops = {
	.allocate = ion_system_heap_allocate,
	.free = ion_system_heap_free,
	.map_dma = ion_system_heap_map_dma,
	.unmap_dma = ion_system_heap_unmap_dma,
	.map_kernel = ion_heap_map_kernel,
	.unmap_kernel = ion_heap_unmap_kernel,
	.map_user = ion_heap_map_user,
};

static int ion_system_heap_shrink(struct shrinker *shrinker,
				  struct shrink_control *sc) {

	struct ion_heap *heap = container_of(shrinker, struct ion_heap,
					     shrinker);
	struct ion_system_heap *sys_heap = container_of(heap,
							struct ion_system_heap,
							heap);
	int nr_total = 0;
	int nr_freed = 0;
	int nr_to_scan = sc->nr_to_scan * 16; /* object size: 64KB (order 4) */
	int i;

	if (nr_to_scan == 0)
		goto end;

	/* shrink the free list first, no point in zeroing the memory if
	   we're just going to reclaim it */
	nr_freed += ion_heap_freelist_drain(heap, sc->nr_to_scan * PAGE_SIZE) /
		PAGE_SIZE;

	if (nr_freed >= nr_to_scan)
		goto end;

	nr_to_scan -= nr_freed;
	/* shrink order: cached pool first, low order pages first */
	for (i = num_orders - 1; i >= 0; i--) {
		struct ion_page_pool *pool = sys_heap->pools[i];

		nr_freed = ion_page_pool_shrink(pool, sc->gfp_mask,
						 nr_to_scan);
		nr_to_scan -= nr_freed;
		if (nr_to_scan <= 0)
			break;

		pool = sys_heap->pools[i + num_orders];
		nr_freed = ion_page_pool_shrink(pool, sc->gfp_mask,
						 nr_to_scan);
		nr_to_scan -= nr_freed;
		if (nr_to_scan <= 0)
			break;
	}

end:
	/* total number of items is whatever the page pools are holding
	   plus whatever's in the freelist */
	for (i = 0; i < num_orders * 2; i++) {
		struct ion_page_pool *pool = sys_heap->pools[i];
		nr_total += ion_page_pool_shrink(pool, sc->gfp_mask, 0);
	}
	nr_total += ion_heap_freelist_size(heap) / PAGE_SIZE;
	return nr_total;

}

unsigned long ion_system_heap_page_pool_total(struct ion_heap *heap)
{
	struct ion_system_heap *sys_heap = container_of(heap,
							struct ion_system_heap,
							heap);
	unsigned long nr_pages = 0;
	int i;

	for (i = 0; i < num_orders * 2; i++) {
		struct ion_page_pool *pool = sys_heap->pools[i];
		nr_pages += ion_page_pool_shrink(pool, __GFP_HIGHMEM, 0);
	}

	nr_pages += (ion_heap_freelist_size(heap) / PAGE_SIZE);

	pr_debug("%s: total %lu pages cached\n", __func__, nr_pages);

	return nr_pages;
}

static int ion_system_heap_debug_show(struct ion_heap *heap, struct seq_file *s,
				      void *unused)
{

	struct ion_system_heap *sys_heap = container_of(heap,
							struct ion_system_heap,
							heap);
	int i;
	for (i = 0; i < num_orders; i++) {
		struct ion_page_pool *pool = sys_heap->pools[i];
		seq_printf(s, "%d order %u highmem pages in cached pool = %lu total\n",
			   pool->high_count, pool->order,
			   (1 << pool->order) * PAGE_SIZE * pool->high_count);
		seq_printf(s, "%d order %u lowmem pages in cached pool = %lu total\n",
			   pool->low_count, pool->order,
			   (1 << pool->order) * PAGE_SIZE * pool->low_count);
	}

	for (i = num_orders; i < (num_orders * 2); i++) {
		struct ion_page_pool *pool = sys_heap->pools[i];
		seq_printf(s, "%d order %u highmem pages in uncached pool = %lu total\n",
			   pool->high_count, pool->order,
			   (1 << pool->order) * PAGE_SIZE * pool->high_count);
		seq_printf(s, "%d order %u lowmem pages in uncached pool = %lu total\n",
			   pool->low_count, pool->order,
			   (1 << pool->order) * PAGE_SIZE * pool->low_count);
	}
	return 0;
}

struct ion_heap *ion_system_heap_create(struct ion_platform_heap *unused)
{
	struct ion_system_heap *heap;
	int i;

	heap = kzalloc(sizeof(struct ion_system_heap), GFP_KERNEL);
	if (!heap)
		return ERR_PTR(-ENOMEM);
	heap->heap.ops = &system_heap_ops;
	heap->heap.type = ION_HEAP_TYPE_SYSTEM;
	heap->heap.flags = ION_HEAP_FLAG_DEFER_FREE;
	heap->pools = kzalloc(sizeof(struct ion_page_pool *) * num_orders * 2,
			      GFP_KERNEL);
	if (!heap->pools)
		goto err_alloc_pools;
	for (i = 0; i < num_orders * 2; i++) {
		struct ion_page_pool *pool;
		gfp_t gfp_flags = low_order_gfp_flags;

		if (orders[i % num_orders] > 4)
			gfp_flags = high_order_gfp_flags;
		pool = ion_page_pool_create(gfp_flags, orders[i % num_orders]);
		if (!pool)
			goto err_create_pool;
		heap->pools[i] = pool;
	}

	heap->heap.shrinker.shrink = ion_system_heap_shrink;
	heap->heap.shrinker.seeks = DEFAULT_SEEKS;
	heap->heap.shrinker.batch = 0;
	register_shrinker(&heap->heap.shrinker);
	heap->heap.debug_show = ion_system_heap_debug_show;
	return &heap->heap;
err_create_pool:
	for (i = 0; i < num_orders * 2; i++)
		if (heap->pools[i])
			ion_page_pool_destroy(heap->pools[i]);
	kfree(heap->pools);
err_alloc_pools:
	kfree(heap);
	return ERR_PTR(-ENOMEM);
}

void ion_system_heap_destroy(struct ion_heap *heap)
{
	struct ion_system_heap *sys_heap = container_of(heap,
							struct ion_system_heap,
							heap);
	int i;

	for (i = 0; i < num_orders; i++)
		ion_page_pool_destroy(sys_heap->pools[i]);
	kfree(sys_heap->pools);
	kfree(sys_heap);
}

static int ion_system_contig_heap_allocate(struct ion_heap *heap,
					   struct ion_buffer *buffer,
					   unsigned long len,
					   unsigned long align,
					   unsigned long flags)
{
	buffer->priv_virt = kzalloc(len, GFP_KERNEL);
	if (!buffer->priv_virt)
		return -ENOMEM;
	return 0;
}

void ion_system_contig_heap_free(struct ion_buffer *buffer)
{
	kfree(buffer->priv_virt);
}

static int ion_system_contig_heap_phys(struct ion_heap *heap,
				       struct ion_buffer *buffer,
				       ion_phys_addr_t *addr, size_t *len)
{
	*addr = virt_to_phys(buffer->priv_virt);
	*len = buffer->size;
	return 0;
}

struct sg_table *ion_system_contig_heap_map_dma(struct ion_heap *heap,
						struct ion_buffer *buffer)
{
	struct sg_table *table;
	int ret;

	table = kzalloc(sizeof(struct sg_table), GFP_KERNEL);
	if (!table)
		return ERR_PTR(-ENOMEM);
	ret = sg_alloc_table(table, 1, GFP_KERNEL);
	if (ret) {
		kfree(table);
		return ERR_PTR(ret);
	}
	sg_set_page(table->sgl, virt_to_page(buffer->priv_virt), buffer->size,
		    0);
	return table;
}

void ion_system_contig_heap_unmap_dma(struct ion_heap *heap,
				      struct ion_buffer *buffer)
{
	sg_free_table(buffer->sg_table);
	kfree(buffer->sg_table);
}

int ion_system_contig_heap_map_user(struct ion_heap *heap,
				    struct ion_buffer *buffer,
				    struct vm_area_struct *vma)
{
	unsigned long pfn = __phys_to_pfn(virt_to_phys(buffer->priv_virt));
	return remap_pfn_range(vma, vma->vm_start, pfn + vma->vm_pgoff,
			       vma->vm_end - vma->vm_start,
			       vma->vm_page_prot);

}

static struct ion_heap_ops kmalloc_ops = {
	.allocate = ion_system_contig_heap_allocate,
	.free = ion_system_contig_heap_free,
	.phys = ion_system_contig_heap_phys,
	.map_dma = ion_system_contig_heap_map_dma,
	.unmap_dma = ion_system_contig_heap_unmap_dma,
	.map_kernel = ion_heap_map_kernel,
	.unmap_kernel = ion_heap_unmap_kernel,
	.map_user = ion_system_contig_heap_map_user,
};

struct ion_heap *ion_system_contig_heap_create(struct ion_platform_heap *unused)
{
	struct ion_heap *heap;

	heap = kzalloc(sizeof(struct ion_heap), GFP_KERNEL);
	if (!heap)
		return ERR_PTR(-ENOMEM);
	heap->ops = &kmalloc_ops;
	heap->type = ION_HEAP_TYPE_SYSTEM_CONTIG;
	return heap;
}

void ion_system_contig_heap_destroy(struct ion_heap *heap)
{
	kfree(heap);
}

