/*
#include <sys/compiler.h>
#include <sys/cpu.h>
#include <sys/log.h>
#include <bsd/list.h>

#include <stdlib.h>
#include <string.h>

struct person {
	const char *name;
	struct node n;
	struct hnode hnode;
};

_unused static void
test0_list(void)
{
	DEFINE_LIST(list);

	struct person daniel  = {.name = "Daniel",  .n = NODE_INIT};
	struct person daniela = {.name = "Daniela", .n = NODE_INIT};
	struct person adam    = {.name = "Adam",    .n = NODE_INIT};
	struct person eve     = {.name = "Eve",     .n = NODE_INIT};
	struct person robot   = {.name = "Robot",   .n = NODE_INIT};

	list_add(&list, &daniel.n);
	list_add(&list, &daniela.n);
	list_add(&list, &adam.n);
	list_add(&list, &eve.n);
	list_add(&list, &robot.n);

	struct person pepa = {.name = "Daniel", .n = NODE_INIT};
	list_add(&list, &pepa.n);
}

_unused static void
test1_list(void)
{
	DEFINE_LIST(list);
	DEFINE_HLIST(hlist);

	struct person daniel  = {.name = "Daniel",  .n = NODE_INIT};
	struct person daniela = {.name = "Daniela", .n = NODE_INIT};
	struct person adam    = {.name = "Adam",    .n = NODE_INIT};
	struct person eve     = {.name = "Eve",     .n = NODE_INIT};
	struct person robot   = {.name = "Robot",   .n = NODE_INIT};

	list_add(&list, &daniel.n);
	list_add(&list, &daniela.n);
	list_add(&list, &adam.n);
	list_add(&list, &eve.n);
	list_add(&list, &robot.n);

	list_for_each(list, n) {
		struct person *p = __container_of(n, struct person, n);
		debug1("node=%p person=%p name=%s", n, p, p->name);
	}

	list_for_each_delsafe(list, n) {
		struct person *p = __container_of(n, struct person, n);
		if (!strcmp(p->name, "Adam"))
			list_del(&p->n);
		debug1("node=%p person=%p name=%s", n, p, p->name);
	}

	struct node *it;
	list_walk(list, it) {
		struct person *p = __container_of(it, struct person, n);
		debug1("node=%p person=%p name=%s", it, p, p->name);
		break;
	}

	struct person *it_person;
	list_walk(list, it_person, n) {
		printf("walk: person name=%s\n", it_person->name);
		break;
	}

	list_walk_next(list, it_person, n) {
		printf("walk_next: person name=%s\n", it_person->name);
	}

	list_walk_delsafe(list, it) {
		struct person *p = __container_of(it, struct person, n);
		debug1("node=%p person=%p name=%s", it, p, p->name);
		list_del(&p->n);
	}

	hlist_add(&hlist, &daniel.hnode);
	hlist_add(&hlist, &daniela.hnode);
	hlist_add(&hlist, &adam.hnode);
	hlist_add(&hlist, &eve.hnode);
	hlist_add(&hlist, &robot.hnode);

	hlist_for_each(&hlist, it) {
		struct person *p = __container_of(it, struct person, hnode);
		printf("name: %s\n", p->name);
	}


}

static inline int
person_cmp(struct person *a, struct person *b)
{
	return strcmp(a->name, b->name);
}

static inline int
person_node_cmp(struct node *a, struct node *b)
{
	return strcmp(__container_of(a, struct person, n)->name, 
	              __container_of(b, struct person, n)->name);
}

_unused static void
test2_list(void)
{
	DEFINE_LIST(list);

	struct person daniel  = {.name = "Daniel",  .n = NODE_INIT};
	struct person daniela = {.name = "Daniela", .n = NODE_INIT};
	struct person adam    = {.name = "Adam",    .n = NODE_INIT};
	struct person eve     = {.name = "Eve",     .n = NODE_INIT};
	struct person robot   = {.name = "Robot",   .n = NODE_INIT};
	struct person daniel1 = {.name = "Daniel",  .n = NODE_INIT};

	list_add(&list, LIST_ITEM(daniel, n));
	list_add(&list, LIST_ITEM(daniela, n));
	list_add(&list, LIST_ITEM(adam, n));
	list_add(&list, LIST_ITEM(eve, n));
	list_add(&list, LIST_ITEM(robot, n));
	list_add(&list, LIST_ITEM(daniel1, n));

	list_sort(&list, person_node_cmp);
	list_for_each(list, n)
		printf("dfn:%s\n", __container_of(n, struct person, n)->name);

	list_sort_asc(&list, person_node_cmp);
	list_for_each(list, n)
		printf("asc:%s\n", __container_of(n, struct person, n)->name);

	list_sort_dsc(&list, person_node_cmp);
	list_for_each(list, n)
		printf("dsc:%s\n", __container_of(n, struct person, n)->name);

	list_sort(&list, person_cmp, struct person, n);
	list_for_each(list, n)
		printf("dfn:%s\n", __container_of(n, struct person, n)->name);

	list_sort_asc(&list, person_cmp, struct person, n);
	list_for_each(list, n)
		printf("asc:%s\n", __container_of(n, struct person, n)->name);

	list_sort_dsc(&list, person_cmp, struct person, n);
	list_for_each(list, n)
		printf("dsc:%s\n", __container_of(n, struct person, n)->name);

	list_sort(&list, person_node_cmp);
	list_ddup(&list, person_node_cmp);
	list_for_each(list, n)
		printf("dup:%s\n", __container_of(n, struct person, n)->name);

	list_sort(&list, person_cmp, struct person, n);
	list_ddup(&list, person_cmp, struct person, n);
	list_for_each(list, n)
		printf("dup:%s\n", __container_of(n, struct person, n)->name);

	list_for_each_delsafe(list, n)
		list_del(n);
}

_unused static void
test3_list(void)
{
	DEFINE_LIST(list);

	struct person daniel  = {.name = "Daniel",  .n = NODE_INIT};
	struct person adam    = {.name = "Adam",    .n = NODE_INIT};
	struct person eve     = {.name = "Eve",     .n = NODE_INIT};

	list_add(&list, &daniel.n);
	list_add(&list, &adam.n);
	list_add(&list, &eve.n);

	list_sort(&list, person_node_cmp);

	list_for_each(list, it)
		printf("a:%s\n", __container_of(it, struct person, n)->name);

	list_for_each(list, it, struct person, n)
		printf("b:%s\n", it->name);

	list_sort_dsc(&list, person_node_cmp);

	list_for_each_delsafe(list, it)
		printf("da:%s\n", __container_of(it, struct person, n)->name);

	list_for_each_delsafe(list, it, struct person, n)
		printf("db:%s\n", it->name);

}

_unused static void
test4_list(void)
{
	DEFINE_LIST(list);

	struct person daniel = { .name = "Daniel", .n = NODE_INIT};
	struct person adam = { .name = "Adam", .n = NODE_INIT};
	struct person eve = { .name = "Eve", .n = NODE_INIT};

	list_add(&list, &daniel.n);
	list_add(&list, &adam.n);
	list_add(&list, &eve.n);

	list_sort_asc(&list, person_node_cmp);
	list_sort_asc(&list, person_cmp, struct person, n);

	struct person *it;
	list_walk(list, it, n) {
		printf("a:%s\n", it->name); break;
	}

	list_walk_next(list, it, n) {
		printf("b:%s\n", it->name); break;
	}

	list_walk_next(list, it, n)
		printf("c:%s\n", it->name);

	list_walk_next(list, it, n)
		printf("d:%s\n", it->name);

	list_walk(list, it, n) {
		printf("e:%s\n", it->name); break;
	}

	list_walk_next(list, it, n)
		printf("f:%s\n", it->name);

}

struct myuser {
	char *name;
	int id;
	struct node n;
};

_unused static inline int
user_cmp_name(struct myuser *a, struct myuser *b)
{
	return strcmp(a->name, b->name);
}
*/
static void
test5_list(void)
{
/*	
	DEFINE_LIST(list);

	list_add(&list, DEFINE_LIST_ITEM(struct myuser, n));
	list_add(&list, DEFINE_LIST_ITEM(struct myuser, n, .name = "Daniel"));
	list_add(&list, DEFINE_LIST_ITEM(struct myuser, n, .name = "Adam", .id = 1));

	list_sort_asc(&list, user_cmp_name, struct myuser, n);

	struct myuser *it;
	list_walk(list, it, n) {
		printf("user name=%s\n", it->name);
	};
*/	
}

int 
main(int argc, char *argv[]) 
{
//	test1_list();
//	test2_list();
//	test3_list();
//	test4_list();

	return 0;
}
