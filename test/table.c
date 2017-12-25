/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2013 Daniel Kubec <niel@rtfm.cz>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"),to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <sys/compiler.h>
#include <sys/cpu.h>
#include <sys/log.h>
#include <mem/alloc.h>
#include <mem/cache.h>
#include <mem/page.h>
#include <list.h>
#include <hash.h>

#define HASHTABLE_BITS 9

DEFINE_HASHTABLE(htable_name, HASHTABLE_BITS);
DEFINE_HASHTABLE(person_age,  HASHTABLE_BITS);

DEFINE_HLIST(person_list);

struct person {
	u32 key;
	char *name;
	unsigned int age;
	struct { 
		struct hnode name; 
		struct hnode age; 
		struct hnode node; 
	} n;
};

struct person daniel  = {.name = "Daniel",  .n.node = INIT_NODE};
struct person daniela = {.name = "Daniela", .n.node = INIT_NODE};
struct person adam    = {.name = "Adam",    .n.node = INIT_NODE};
struct person eve     = {.name = "Eve",     .n.node = INIT_NODE};
struct person robot   = {.name = "Robot",   .n.node = INIT_NODE};

int
main(void)
{
	hash_init(htable_name);
	hash_init(person_age);

	hlist_add(&person_list, &daniel.n.node);
	hlist_add(&person_list, &daniela.n.node);
	hlist_add(&person_list, &adam.n.node);
	hlist_add(&person_list, &eve.n.node);
	hlist_add(&person_list, &robot.n.node);

	hlist_for_each(&person_list, it) {
		struct person *p = __container_of(it, struct person, n.node);
		p->key   = hash_buffer(p->name, strlen(p->name));
		u32 hash = hash_data(htable_name, p->key);
		hash_add(htable_name, &p->n.name, hash);
	}

	u32 key = hash_skey(htable_name, "Daniel");
	hash_for_each(htable_name, it, key) {
		struct person *p = __container_of(it, struct person, n.node);
		printf(":: name: %s\n", p->name);
	}

	return 0;
}
