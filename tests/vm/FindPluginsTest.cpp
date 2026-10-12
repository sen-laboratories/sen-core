/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

/**
 * Finds the SENSEI plugins like the server does (sen::FindPlugins: a query for the plugin signatures, then the type and the feature are read
 * from the attributes) and checks that the plugins that are installed or built are all there. Does not need the server. Haiku only.
 * Names of the binaries that must be found are given as arguments, per feature: extract=SenPdfExtractor,SenMimeExtractor navigate=...
 */
#include <stdio.h>
#include <string.h>

#include <set>
#include <string>
#include <vector>

#include <Path.h>

#include "../../src/relations/QueryUtil.h"

int
main(int argc, char** argv)
{
	int failed = 0;
	for (int i = 1; i < argc; i++) {
		std::string argument = argv[i];
		size_t equals = argument.find('=');
		if (equals == std::string::npos)
			continue;
		std::string feature = argument.substr(0, equals);

		std::vector<entry_ref> found;
		status_t result = sen::FindPlugins(feature.c_str(), &found);
		std::set<std::string> names;
		for (const entry_ref& ref : found)
			names.insert(ref.name);
		printf("%s: %d found (%s)\n", feature.c_str(), (int) found.size(), strerror(result));
		for (const std::string& name : names)
			printf("  %s\n", name.c_str());

		size_t start = equals + 1;
		while (start < argument.size()) {
			size_t end = argument.find(',', start);
			if (end == std::string::npos)
				end = argument.size();
			std::string expected = argument.substr(start, end - start);
			if (names.count(expected) == 0) {
				printf("FAIL %s is not found for %s\n", expected.c_str(), feature.c_str());
				failed++;
			} else {
				printf("ok   %s is found for %s\n", expected.c_str(), feature.c_str());
			}
			start = end + 1;
		}
	}
	printf("%s\n", failed == 0 ? "all found" : "FAILED");
	return failed == 0 ? 0 : 1;
}
