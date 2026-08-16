#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <stddef.h>
struct Hnode
{
    Hnode *next = NULL;
    uint64_t hcode = 0;
};


struct HTab
{
    Hnode **tab = NULL;
    size_t mask = 0;
    size_t size = 0;
};

struct Hmap
{
    HTab ht1;
    HTab ht2;
    size_t resizing_pos = 0;
};
#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))

const size_t k_resizing_work = 128;
const size_t k_max_load_factor = 8;

static void h_init(HTab *htab, size_t n)
{
    assert(n > 0 && ((n - 1) & n) == 0);
    htab->tab = (Hnode **)calloc(n, sizeof(Hnode *));
    htab->mask = n - 1;
    htab->size = 0;
}

static void h_insert(HTab *htab, Hnode *node)
{
    size_t pos = node->hcode & htab->mask;
    Hnode *next = htab->tab[pos];
    node->next = next;
    htab->tab[pos] = node;
    htab->size++;
}
static size_t hm_size(Hmap *hmap) {
    return hmap->ht1.size + hmap->ht2.size;
}
static Hnode **h_lookup(HTab *htab, Hnode *key, bool (*eq)(Hnode *, Hnode *))
{
    if (!htab->tab)
    {
        return NULL;
    }
    size_t pos = key->hcode & htab->mask;
    Hnode **from = &htab->tab[pos];

    while (*from)
    {
        if ((*from)->hcode == key->hcode && eq(*from, key))
        {
            return from;
        }
        from = &(*from)->next;
    }
    return NULL;
}

static Hnode *h_detach(HTab *htab, Hnode **from)
{
    Hnode *node = *from;
    *from = node->next;
    htab->size--;
    return node;
}
static void hm_help_resizing(Hmap *hmap)
{
    if (hmap->ht2.tab == NULL)
    {
        return;
    }

    size_t nwork = 0;
    while (nwork < k_resizing_work && hmap->ht1.size > 0)
    {
        Hnode **from = &hmap->ht1.tab[hmap->resizing_pos];
        if (*from == NULL)
        {
            hmap->resizing_pos++;
            continue;
        }

        h_insert(&hmap->ht2, h_detach(&hmap->ht1, from));
        nwork++;
    }

    if (hmap->ht1.size == 0)
    {
        free(hmap->ht1.tab);
        hmap->ht1 = hmap->ht2;
        hmap->ht2 = HTab{};
        hmap->resizing_pos = 0;
    }
}

static void hm_trigger_resizing(Hmap *hmap)
{
    if (hmap->ht2.tab != NULL)
    {
        return;
    }

    size_t bigger = (hmap->ht1.mask + 1) * 2;
    h_init(&hmap->ht2, bigger);
    hmap->resizing_pos = 0;
}
void hm_insert(Hmap *hmap, Hnode *node)
{
    if (!hmap->ht1.tab)
    {
        h_init(&hmap->ht1, 4);
    }
    h_insert(&hmap->ht1, node);
    size_t load = hmap->ht1.size / (hmap->ht1.mask + 1);
    if (load >= k_max_load_factor)
    {
        hm_trigger_resizing(hmap);
    }
    hm_help_resizing(hmap);
}
struct Hnode *hm_lookup(Hmap *hmap, Hnode *key, bool (*eq)(Hnode *, Hnode *))
{
    hm_help_resizing(hmap);
    Hnode **from = h_lookup(&hmap->ht1, key, eq);
    if (!from)
    {
        from = h_lookup(&hmap->ht2, key, eq);
    }
    return from ? *from : NULL;
}

Hnode *hm_delete(Hmap *hmap, Hnode *key, bool (*eq)(Hnode *, Hnode *))
{

    hm_help_resizing(hmap);
    Hnode **from = h_lookup(&hmap->ht1, key, eq);
    if (from)
    {
        return h_detach(&hmap->ht1, from);
    }

    from = h_lookup(&hmap->ht2, key, eq);
    if (from)
    {
        return h_detach(&hmap->ht2, from);
    }

    return NULL;
}

void hm_destroy(Hmap *hmap)
{
    free(hmap->ht1.tab);
    free(hmap->ht2.tab);
    *hmap = Hmap{};
}
