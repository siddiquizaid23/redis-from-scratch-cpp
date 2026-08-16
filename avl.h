#pragma once 
#include <stddef.h>
#include<stdint.h>

struct AVLNode
{
    uint32_t depth = 0;
    uint32_t cnt = 0;
    AVLNode *left = NULL;
    AVLNode *right = NULL;
    AVLNode  *parent = NULL;

};

static uint32_t avl_depth (AVLNode *node){
    return node ? node->depth : 0;

}
static uint32_t avl_cnt(AVLNode *node){
    return node ? node->cnt : 0;
}

static void avl_update(AVLNode *node){
    uint32_t l = avl_depth(node->left);
    uint32_t r = avl_depth(node->right );
    node->depth  = 1 + (l>r ? l : r);
    node->cnt = 1 + avl_cnt(node->left) + avl_cnt(node->right);
}


static AVLNode *rotate_left(AVLNode *node){
    AVLNode *parent = node->parent;
    AVLNode *B = node->right;
    AVLNode *Y = B->left;

    B->left = node;
    B->parent = parent;
    node ->right = Y ;
    node->parent = B;

    if(Y){Y->parent = node;}

    if(parent ){
        if(parent->left == node){
            parent->left = B;

        }
        else{
            parent->right = B;
        }
    }
    avl_update(node);
    avl_update(B);
    return B;
}


static AVLNode *rotate_right(AVLNode *node){
    AVLNode *parent = node->parent;
    AVLNode *B = node->left;
    AVLNode *Y = B->right;

    B->right = node;
    B->parent = parent;
    node->left = Y;
    node->parent = B;
    if(Y){Y->parent = node;}
    if(parent ){
        if(parent->left == node){parent->left = B;}
    else {
          parent->right = B;
    }}
    avl_update(node);
    avl_update(B);
    return B;
    
}



static AVLNode *avl_fix(AVLNode *node){
    while(true){
        avl_update(node);
        uint32_t l = avl_depth(node->left);
        uint32_t r = avl_depth(node->right);
        AVLNode **from = NULL;

        if(node->parent){
            from = (node->parent->left == node) ? &node->parent->left : &node->parent->right;
        }
        if( l == r+2){
            if(avl_depth(node->left->left) >= avl_depth(node->left->right)){
                node = rotate_right(node);
            }
            else{
                rotate_left(node->left);
                node= rotate_right(node);
            }
        }
        else if ( r  == l + 2){
            if(avl_depth(node->right->right ) >= avl_depth(node->right->left)){
                node = rotate_left(node);
            }
            else{
                rotate_right(node->right);
                node = rotate_left(node);
            }
        }
        
           if(from){ *from = node ;}

           if(!node->parent) {
            return node;
           }
              node = node->parent;
    }
    return node;
}

static AVLNode *avl_del(AVLNode * node){
    if(node->right == NULL){
        AVLNode  *parent = node->parent;
        if(node->left){
            node->left->parent = parent;
        }
        if(parent){
            if(parent->left == node){
                parent->left = node->left;
            }
            else{

            parent->right = node->left;
            }
            return avl_fix(parent);
        }
        else{
            return node->left;
        }
    }else{
        AVLNode *victim = node->right;
        while (victim->left){victim = victim->left; }
        
        AVLNode *root = avl_del(victim);
        *victim  = *node;

        if(victim->left){victim->left->parent = victim;}
        if(victim->right){victim->right->parent = victim ;}

        AVLNode *parent = victim->parent;
        if(parent){
            if(parent->left == node){parent->left = victim;}
            else{parent->right  = victim; }
            return root ? root : victim;
        }

        return victim;
    }
}
static AVLNode *avl_offset(AVLNode *node, int64_t offset){
         if (!node) {
             return NULL;
         }

         auto next_node = [](AVLNode *cur) -> AVLNode * {
             if (cur->right) {
                 cur = cur->right;
                 while (cur->left) {
                     cur = cur->left;
                 }
                 return cur;
             }

             while (cur->parent && cur->parent->right == cur) {
                 cur = cur->parent;
             }
             return cur->parent;
         };

         auto prev_node = [](AVLNode *cur) -> AVLNode * {
             if (cur->left) {
                 cur = cur->left;
                 while (cur->right) {
                     cur = cur->right;
                 }
                 return cur;
             }

             while (cur->parent && cur->parent->left == cur) {
                 cur = cur->parent;
             }
             return cur->parent;
         };

         while (offset > 0) {
             node = next_node(node);
             if (!node) {
                 return NULL;
             }
             offset--;
         }

         while (offset < 0) {
             node = prev_node(node);
             if (!node) {
                 return NULL;
             }
             offset++;
         }

         return node;
         
    }
