#pragma once

#include <stdint.h>

struct dlist
{
	dlist *Next;
	dlist *Prev;
};

inline bool
empty(dlist *L)
{
	return L->Next == L;
}

inline size_t
size(dlist *L)
{
	size_t i = 0;
	for(dlist *I = L->Next; I != L; I = I->Next) ++i;
	return i;
}

inline void
init(dlist *Sentinel)
{
	Sentinel->Next = Sentinel;
	Sentinel->Prev = Sentinel;
}

inline void
insert_first(dlist *Sentinel, dlist *Element)
{
	Element->Prev       = Sentinel;
	Element->Next       = Sentinel->Next;
	Element->Next->Prev = Element;
	Element->Prev->Next = Element;
}

inline void
insert_last(dlist *Sentinel, dlist *Element)
{
	Element->Next       = Sentinel;
	Element->Prev       = Sentinel->Prev;
	Element->Next->Prev = Element;
	Element->Prev->Next = Element;
}

inline void
remove(dlist *Element)
{
	Element->Prev->Next = Element->Next;
	Element->Next->Prev = Element->Prev;
}

#define ContainerOf(Member, type, member) \
	(type *)(void *)((uint8_t *)(Member) - __builtin_offsetof(type, member))

#define dlist_for(x, y, type, member)                                    \
	type  *x;                                                            \
	dlist *x##_iter = (y)->Next;                                         \
	for((x)      = ContainerOf(x##_iter, type, member); x##_iter != (y); \
	    x##_iter = x##_iter->Next, (x) = ContainerOf(x##_iter, type, member))
