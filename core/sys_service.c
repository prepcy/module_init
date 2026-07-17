/**
 * @file sys_service.c
 * @brief 类型化服务注册中心实现。
 */

#include <pthread.h>
#include <stdbool.h>

#include "sys_internal.h"
#include "sys_service.h"

#define SYS_SERVICE_MAX_ENTRIES 64U

typedef struct {
	bool used;
	bool accepting;
	size_t reference_count;
	sys_service_desc_t desc;
} sys_service_slot_t;

static sys_service_slot_t g_service_slots[SYS_SERVICE_MAX_ENTRIES];
static pthread_mutex_t g_service_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_service_cond = PTHREAD_COND_INITIALIZER;

static sys_service_slot_t *find_service(uint32_t module_id, uint32_t interface_id)
{
	for (size_t i = 0; i < SYS_SERVICE_MAX_ENTRIES; i++) {
		sys_service_slot_t *slot = &g_service_slots[i];

		if (slot->used && slot->desc.module_id == module_id && slot->desc.interface_id == interface_id) {
			return slot;
		}
	}
	return NULL;
}

sys_err_t sys_service_register(const sys_service_desc_t *desc)
{
	sys_service_slot_t *free_slot = NULL;

	if (desc == NULL || desc->module_id == 0U || desc->interface_id == 0U || desc->abi_version == 0U ||
	    desc->ops == NULL || desc->ops_size == 0U || desc->name == NULL) {
		return SYS_ERR_INVALID_PARAM;
	}

	pthread_mutex_lock(&g_service_mutex);
	if (find_service(desc->module_id, desc->interface_id) != NULL) {
		pthread_mutex_unlock(&g_service_mutex);
		return SYS_ERR_ALREADY_EXISTS;
	}

	for (size_t i = 0; i < SYS_SERVICE_MAX_ENTRIES; i++) {
		if (!g_service_slots[i].used) {
			free_slot = &g_service_slots[i];
			break;
		}
	}
	if (free_slot == NULL) {
		pthread_mutex_unlock(&g_service_mutex);
		return SYS_ERR_NO_MEMORY;
	}

	free_slot->used = true;
	free_slot->accepting = true;
	free_slot->reference_count = 0U;
	free_slot->desc = *desc;
	pthread_mutex_unlock(&g_service_mutex);
	return SYS_OK;
}

sys_err_t sys_service_unregister(uint32_t module_id, uint32_t interface_id)
{
	sys_service_slot_t *slot;

	pthread_mutex_lock(&g_service_mutex);
	slot = find_service(module_id, interface_id);
	if (slot == NULL) {
		pthread_mutex_unlock(&g_service_mutex);
		return SYS_ERR_NOT_FOUND;
	}

	slot->accepting = false;
	while (slot->reference_count != 0U) {
		pthread_cond_wait(&g_service_cond, &g_service_mutex);
	}
	*slot = (sys_service_slot_t){0};
	pthread_mutex_unlock(&g_service_mutex);
	return SYS_OK;
}

sys_err_t sys_service_acquire(uint32_t module_id, uint32_t interface_id, uint32_t abi_version, size_t minimum_ops_size,
			      sys_service_ref_t *out_ref)
{
	sys_service_slot_t *slot;

	if (out_ref == NULL) {
		return SYS_ERR_INVALID_PARAM;
	}
	out_ref->ops = NULL;
	out_ref->internal = NULL;
	if (module_id == 0U || interface_id == 0U || abi_version == 0U || minimum_ops_size == 0U) {
		return SYS_ERR_INVALID_PARAM;
	}

	pthread_mutex_lock(&g_service_mutex);
	slot = find_service(module_id, interface_id);
	if (slot == NULL || !slot->accepting) {
		pthread_mutex_unlock(&g_service_mutex);
		return SYS_ERR_NOT_FOUND;
	}
	if (slot->desc.abi_version != abi_version || slot->desc.ops_size < minimum_ops_size) {
		pthread_mutex_unlock(&g_service_mutex);
		return SYS_ERR_ABI_MISMATCH;
	}

	if (slot->reference_count == SIZE_MAX) {
		pthread_mutex_unlock(&g_service_mutex);
		return SYS_ERR_OVERFLOW;
	}
	slot->reference_count++;
	out_ref->ops = slot->desc.ops;
	out_ref->internal = slot;
	pthread_mutex_unlock(&g_service_mutex);
	return SYS_OK;
}

void sys_service_release(sys_service_ref_t *ref)
{
	sys_service_slot_t *slot;

	if (ref == NULL || ref->internal == NULL) {
		return;
	}

	slot = ref->internal;
	pthread_mutex_lock(&g_service_mutex);
	if (slot->reference_count > 0U) {
		slot->reference_count--;
		if (slot->reference_count == 0U) {
			pthread_cond_broadcast(&g_service_cond);
		}
	}
	pthread_mutex_unlock(&g_service_mutex);
	ref->ops = NULL;
	ref->internal = NULL;
}

size_t sys_service_get_status(sys_service_status_t *statuses, size_t capacity)
{
	size_t total = 0U;
	size_t copied = 0U;

	pthread_mutex_lock(&g_service_mutex);
	for (size_t i = 0; i < SYS_SERVICE_MAX_ENTRIES; i++) {
		const sys_service_slot_t *slot = &g_service_slots[i];

		if (!slot->used) {
			continue;
		}
		if (statuses != NULL && copied < capacity) {
			statuses[copied++] = (sys_service_status_t){.module_id = slot->desc.module_id,
								    .interface_id = slot->desc.interface_id,
								    .abi_version = slot->desc.abi_version,
								    .name = slot->desc.name,
								    .reference_count = slot->reference_count,
								    .accepting = slot->accepting};
		}
		total++;
	}
	pthread_mutex_unlock(&g_service_mutex);
	return total;
}

void sys_service_registry_reset(void)
{
	pthread_mutex_lock(&g_service_mutex);
	for (size_t i = 0; i < SYS_SERVICE_MAX_ENTRIES; i++) {
		g_service_slots[i].accepting = false;
	}
	for (;;) {
		bool has_references = false;

		for (size_t i = 0; i < SYS_SERVICE_MAX_ENTRIES; i++) {
			if (g_service_slots[i].reference_count != 0U) {
				has_references = true;
				break;
			}
		}
		if (!has_references) {
			break;
		}
		pthread_cond_wait(&g_service_cond, &g_service_mutex);
	}
	for (size_t i = 0; i < SYS_SERVICE_MAX_ENTRIES; i++) {
		g_service_slots[i] = (sys_service_slot_t){0};
	}
	pthread_mutex_unlock(&g_service_mutex);
}
