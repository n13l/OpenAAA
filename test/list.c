#include <sys/compiler.h>
#include <sys/cpu.h>
#include <sys/log.h>

#include <list.h>
#include <iter.h>

#include <stdlib.h>
#include <string.h>

struct person {
	const char *name;
	struct node node;
};

#define DECLARE_PERSON(name) \
	({ (struct person) {.name = ##name,  .node = INIT_NODE}; })

static void
test0_list(void)
{
	DECLARE_LIST(list);

	struct person daniel  = {.name = "Daniel",  .node = INIT_NODE};
	struct person daniela = {.name = "Daniela", .node = INIT_NODE};
	struct person adam    = {.name = "Adam",    .node = INIT_NODE};
	struct person eve     = {.name = "Eve",     .node = INIT_NODE};
	struct person robot   = {.name = "Robot",   .node = INIT_NODE};

	list_add(&list, &daniel.node);
	list_add(&list, &daniela.node);
	list_add(&list, &adam.node);
	list_add(&list, &eve.node);
	list_add(&list, &robot.node);

	struct person pepa = {.name = "Daniel", .node = INIT_NODE};

	list_add(&list, &pepa.node);

	struct person *p;
	for (p = it_begin(list, p, node); p; p = it_next(list, p, node)) {
		printf("person=%p name=%s\n", p, p->name);
	}

	it_for_each(list, p, node) {
		printf("person=%p name=%s\n", p, p->name);
	}
}

static void
test1_list(void)
{
	DECLARE_LIST(list);

	struct person daniel  = {.name = "Daniel",  .node = INIT_NODE};
	struct person daniela = {.name = "Daniela", .node = INIT_NODE};
	struct person adam    = {.name = "Adam",    .node = INIT_NODE};
	struct person eve     = {.name = "Eve",     .node = INIT_NODE};
	struct person robot   = {.name = "Robot",   .node = INIT_NODE};

	list_add(&list, &daniel.node);
	list_add(&list, &daniela.node);
	list_add(&list, &adam.node);
	list_add(&list, &eve.node);
	list_add(&list, &robot.node);

	/* iterate over all objects */
	list_for_each(n, list) {
		struct person *p = __container_of(n, struct person, node);
		printf("node=%p person=%p name=%s\n", n, p, p->name);
	}

	/* iterate and unlink adam */
	list_for_each_delsafe(n, list) {
		struct person *p = __container_of(n, struct person, node);
		if (!strcmp(p->name, "Adam"))
			list_del(&p->node);
		printf("node=%p person=%p name=%s\n", n, p, p->name);
	}

	struct node *cursor = &daniela.node;
	/* iterate over rest: starts at daniela node */
	list_walk(cursor, n, list) {
		struct person *p = __container_of(n, struct person, node);
		debug("node=%p person=%p name=%s", n, p, p->name);
		break;
	}
	/* iterate over rest with del safety: starts at daniel node */
	list_walk_delsafe(cursor, n, list) {
		struct person *p = __container_of(n, struct person, node);
		debug("node=%p person=%p name=%s", n, p, p->name);
		list_del(&p->node);
	}


}

static void
test2_list(void)
{
	DEFINE_LIST(list);
	list_init(&list);

	struct person daniel  = {.name = "Daniel",  .node = INIT_NODE};
	struct person daniela = {.name = "Daniela", .node = INIT_NODE};
	struct person adam    = {.name = "Adam",    .node = INIT_NODE};
	struct person eve     = {.name = "Eve",     .node = INIT_NODE};
	struct person robot   = {.name = "Robot",   .node = INIT_NODE};

	list_add_tail(&list, &daniel.node);
	list_add_tail(&list, &daniela.node);
	list_add_tail(&list, &adam.node);
	list_add_tail(&list, &eve.node);
	list_add_tail(&list, &robot.node);

	list_for_each_delsafe(n, list)
		list_del(n);

}

int 
main(int argc, char *argv[]) 
{
//	test0_list();
	test1_list();
	test2_list();
	return 0;
}