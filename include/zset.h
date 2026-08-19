#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include "avl.h"
#include "hashtable.h"

static uint64_t str_hash(const uint8_t *data, size_t len)
{
    uint32_t h = 0x811C9DC5;
    for (size_t i = 0; i < len; i++)
    {
        h = (h + data[i]) * 0x01000193;
    }
    return h;
}

struct ZNode
{
    AVLNode tree;
    Hnode hmap;
    double score = 0 ;
    size_t len = 0;
    char name[0];
};
struct ZSet
{
    AVLNode *tree = NULL;
    Hmap hmap ;
};


static ZNode *znode_new(const char *name , size_t len , double score){

    ZNode *node = (ZNode *)malloc(sizeof(ZNode)+len);
assert(node);
node->tree = AVLNode{};
node->tree.depth = 1 ;
node->tree.cnt = 1 ;
node->hmap = Hnode{};

node->score =score;
node->len =len;


memcpy(node->name,name,len);
return node;

}
static int zless(AVLNode *lhs_avl,double score,const char *name,size_t len)
{
    ZNode *lhs = container_of(lhs_avl,ZNode,tree);
    if(lhs->score != score){
  return lhs->score < score ? -1 :1 ;
    }

    size_t minlen = lhs->len < len ? lhs->len : len;
     int rv = memcmp(lhs->name, name,minlen);
     if(rv != 0 )
     { return rv;}

     if(lhs->len != len){
        return lhs->len < len ? -1 : 1 ;
     }
     return 0;
}


static void tree_insert(ZSet *zset, ZNode *node){
    AVLNode *cur = NULL;
    AVLNode **from = &zset->tree;

    while (*from)
    {
        cur = *from;
        int cmp = zless(cur, node->score,node->name,node->len);
        from = cmp < 0 ? &cur->right : &cur->left;
    }
    *from = &node->tree;
    node->tree.parent = cur ;
    zset->tree = avl_fix(&node->tree);
    
}




struct HKey
{
    Hnode node;
    const char *name = NULL;
    size_t len = 0;
};
 static bool hcmp(Hnode *node ,Hnode  *key){
    ZNode *znode = container_of(node, ZNode ,hmap);
    HKey * hkey = container_of(key,HKey,node);
    if(znode->len != hkey->len){
        return false;
    }
    return memcmp(znode->name,hkey->name,znode->len) == 0;
 }
ZNode *zset_lookup(ZSet *zset, const char *name, size_t len) {
    if (!zset->tree) { return NULL; }

    HKey key;
    key.node.hcode = str_hash((uint8_t *)name, len);
    key.name       = name;
    key.len        = len;

    Hnode *found = hm_lookup(&zset->hmap, &key.node, hcmp);
    return found ? container_of(found, ZNode, hmap) : NULL;
}
bool zadd(ZSet *zset, const char *name, size_t len , double score){
    ZNode  *node = zset_lookup(zset,name,len);
    if(node){
  zset->tree = avl_del(&node->tree);
  node->score = score;
  avl_update(&node->tree);
  tree_insert(zset,node);
  return false;
    }
    else{

        node = znode_new(name,len,score);
        node->hmap.hcode = str_hash((uint8_t *)name, len);
        hm_insert(&zset->hmap,&node->hmap);
    tree_insert(zset,node);
return true;
    }
}

ZNode *zset_pop(ZSet *zset, const char *name,size_t len){
    ZNode *node = zset_lookup(zset,name,len);
    if(!node){
        return NULL;
    }
    zset->tree= avl_del(&node->tree);

    HKey key;
    key.node.hcode = str_hash((uint8_t *)name, len);
    key.name = name;
    key.len = len ;
    hm_delete(&zset->hmap,&key.node , hcmp);

    return node;
}

void znode_free(ZNode *node){
    free(node);
}

ZNode *zset_query(ZSet *zset,double score,const char *name,size_t len){
    AVLNode *found = NULL;
    AVLNode *cur = zset->tree;
    while(cur){
        int cmp = zless(cur,score,name,len);
        if(cmp<0){
            cur = cur->right;
        }else{
            found = cur;
            cur = cur->left;

        }
    }
    return found ? container_of(found ,ZNode,tree) : NULL;
}

ZNode *znode_offset(ZNode *node,int64_t offset){
    AVLNode *found = avl_offset(&node->tree,offset);
    return found ? container_of(found,ZNode,tree) : NULL;
}