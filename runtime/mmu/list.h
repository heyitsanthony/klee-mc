#ifndef LIST_H
#define LIST_H

#include <stdint.h>

/* doubly linked circular list with dummy node */
typedef struct list_item list_item;
typedef struct list list;

struct list_item{
	list_item	*li_next;
	list_item	*li_prev;
};

struct list{
	list_item	lst_list;
	int		lst_offset;	/* how far forward offset from data */
};

static inline void list_init(list* l, int off)
{
	l->lst_list.li_next = &l->lst_list;
	l->lst_list.li_prev = &l->lst_list;
	l->lst_offset = off;
}

static inline void list_item_init(list_item* li)
{
	li->li_next = NULL;
	li->li_prev = NULL;
}

static inline list_item* list_peek_head(list* l)
{
	if(&l->lst_list != l->lst_list.li_next)
		return l->lst_list.li_next;
	else
		return NULL;
}

static inline list_item* list_peek_tail(list* l)
{
	if(&l->lst_list != l->lst_list.li_prev)
		return l->lst_list.li_prev;
	else
		return NULL;
}

static inline int list_is_empty(list* l)
{
	return (&l->lst_list == l->lst_list.li_next);
}

static inline void list_add_head(list* l, list_item* toadd)
{
	list_item*	ltop = &l->lst_list;
	toadd->li_prev = ltop;
	toadd->li_next = ltop->li_next;
	ltop->li_next->li_prev = toadd;
	ltop->li_next = toadd;
}

static inline void list_add_tail(list* l, list_item* toadd)
{
	list_item*	ltop = &l->lst_list;
	toadd->li_next = ltop;
	toadd->li_prev = ltop->li_prev;
	ltop->li_prev->li_next = toadd;
	ltop->li_prev = toadd;
}

static inline void list_insert_after(list_item* chain, list_item* toadd)
{
	toadd->li_prev = chain;
	toadd->li_next = chain->li_next;
	chain->li_next->li_prev = toadd;
	chain->li_next = toadd;
}

static inline void list_insert_before(list_item* chain, list_item* toadd)
{
	toadd->li_next = chain;
	toadd->li_prev = chain->li_prev;
	chain->li_prev->li_next = toadd;
	chain->li_prev = toadd;
}

static inline void list_remove(list_item* elem)
{
	elem->li_next->li_prev = elem->li_prev;
	elem->li_prev->li_next = elem->li_next;
	elem->li_prev = NULL;
	elem->li_next = NULL;
}

static inline list_item* list_remove_head(list* l)
{
	if(l->lst_list.li_next != &l->lst_list){
		list_item	*head;
		head = l->lst_list.li_next;
		list_remove(head);
		return head;
	}else
		return NULL;
}

static inline list_item* list_remove_tail(list* l)
{
	if(l->lst_list.li_prev != &l->lst_list){
		list_item	*tail;
		list_remove(l->lst_list.li_prev);
		return tail;
	}else
		return NULL;
}

static inline void* list_get_data(list* l, list_item* elem)
{
	if(elem == NULL) 
		return NULL;
	return (void*)((intptr_t)elem - l->lst_offset);
}

static inline void list_clear(list* l)
{
	l->lst_list.li_next = &l->lst_list;
	l->lst_list.li_prev = &l->lst_list;
}

static inline int list_item_orphan(list_item* elem)
{
	return (elem->li_next == NULL && elem->li_prev == NULL);
}

/* move the contents of one list to another */
static inline void list_set_list(list* newlist, list* oldlist)
{
	list_item	*newlist_head, *oldlist_head;

	newlist_head = &newlist->lst_list;
	oldlist_head = &oldlist->lst_list;
	
	if(list_is_empty(oldlist))
		return;
	
	oldlist_head->li_next->li_prev = newlist_head;
	oldlist_head->li_prev->li_next = newlist_head;
	newlist_head->li_next = oldlist_head->li_next;
	newlist_head->li_prev = oldlist_head->li_prev;

	/* prevent aliasing issues */
	list_clear(oldlist);
}

static inline void* list_get_next(list* l, void* elem)
{
	list_item	*next;
	
	if(elem == NULL) 
		return NULL;

	next = ((list_item*)(void*)((intptr_t)elem + l->lst_offset))->li_next;
	if(next == &l->lst_list)
		return NULL;
	
	return list_get_data(l, next);
}

static inline void* list_get_prev(list* l, void* elem)
{
	list_item	*prev;
	
	if(elem == NULL) 
		return NULL;

	prev = ((list_item*)(void*)((intptr_t)elem + l->lst_offset))->li_prev;
	if(prev == &l->lst_list)
		return NULL;
	
	return list_get_data(l, prev);
}


#define list_from(x, y)		for(; y != &((x)->lst_list); y = y->li_next)
#define list_for_all(x, y)	for(y = (x)->lst_list.li_next; y != &((x)->lst_list); y = y->li_next)
#define list_for_all_safe(x,y,z)\
	for(y = (x)->lst_list.li_next, z = y->li_next; y != &((x)->lst_list); y = z, z = y->li_next)
#define list_for_all_backwards(x, y) for(y = (x)->lst_list.li_prev; y != &((x)->lst_list); y = y->li_prev)
#define list_for_all_backwards_safe(x,y,z)\
	for(y = (x)->lst_list.li_prev, z = y->li_prev; y != &((x)->lst_list); y = z, z = y->li_prev)
		
#endif
