/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

/**
 * Builds the menu of the item of SEN in the Deskbar (without the Deskbar) and prints it as a tree, then checks the order of the entries:
 * Ontologies above Settings, Restart above Quit, and that the menu of an ontology has its types. Haiku only; the result depends on what is installed.
 */
#include <Application.h>
#include <Menu.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <stdio.h>
#include <string.h>

#include <string>
#include <vector>

#define private public
#include "../../src/deskbar/DeskbarView.h"
#undef private

static void
Print(BMenu* menu, int depth, std::vector<std::string>* top)
{
	for (int32 i = 0; i < menu->CountItems(); i++) {
		BMenuItem* item = menu->ItemAt(i);
		if (item->Submenu() != NULL)
			printf("%*s%s >\n", depth * 3, "", item->Label());
		else if (item->Label() != NULL)
			printf("%*s%s\n", depth * 3, "", item->Label());
		else
			printf("%*s---\n", depth * 3, "");
		if (depth == 0)
			top->push_back(item->Label() != NULL ? item->Label() : "---");
		if (item->Submenu() != NULL)
			Print(item->Submenu(), depth + 1, top);
	}
}

static int
Index(const std::vector<std::string>& list, const char* label)
{
	for (size_t i = 0; i < list.size(); i++) {
		if (list[i].compare(0, strlen(label), label) == 0)
			return (int) i;
	}
	return -1;
}

int
main()
{
	BApplication app("application/x-vnd.sen-labs.menu-test");
	sen::deskbar::DeskbarView view(BRect(0, 0, 15, 15));
	BPopUpMenu* menu = view._BuildMenu();

	std::vector<std::string> top;
	Print(menu, 0, &top);

	int failed = 0;
	auto check = [&](const char* what, bool ok) { printf("%s %s\n", ok ? "ok  " : "FAIL", what); if (!ok) failed++; };
	int ontologies = Index(top, "Ontologies"), settings = Index(top, "Settings"), restart = Index(top, "Restart"), quit = Index(top, "Quit");
	check("Ontologies is directly above Settings", ontologies >= 0 && settings == ontologies + 1);
	check("Restart is directly above Quit", restart >= 0 && quit == restart + 1);
	BMenuItem* item = menu->ItemAt(ontologies);
	check("Ontologies has a menu with Show all and Update first", item != NULL && item->Submenu() != NULL && item->Submenu()->CountItems() >= 2
		&& strncmp(item->Submenu()->ItemAt(0)->Label(), "Show all", 8) == 0 && strcmp(item->Submenu()->ItemAt(1)->Label(), "Update") == 0);
	// the ontologies have a menu each, with their types, which show their attributes (the result depends on what is installed)
	bool types = false, attributes = false;
	if (item != NULL && item->Submenu() != NULL) {
		for (int32 i = 2; i < item->Submenu()->CountItems(); i++) {
			BMenu* ontology = item->Submenu()->SubmenuAt(i);
			if (ontology == NULL)
				continue;
			for (int32 j = 0; j < ontology->CountItems(); j++) {
				types = types || ontology->ItemAt(j)->Label() != NULL;
				BMenu* type = ontology->SubmenuAt(j);
				attributes = attributes || (type != NULL && type->CountItems() > 2);
			}
		}
	}
	check("the ontologies show their types", types);
	check("a type shows its attributes", attributes);
	// the contexts are listed, the global one first, each with a message that sets it
	BMenuItem* context = menu->ItemAt(Index(top, "Context"));
	check("the Context menu lists the contexts, global first",
		context != NULL && context->Submenu() != NULL && context->Submenu()->CountItems() >= 1
		&& strcmp(context->Submenu()->ItemAt(0)->Label(), "global") == 0 && context->Submenu()->ItemAt(0)->Message() != NULL);
	printf("%s\n", failed == 0 ? "all ok" : "FAILED");
	delete menu;
	return failed == 0 ? 0 : 1;
}
