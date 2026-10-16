/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

/**
 * Tests of the registry of the semantic types (SenTypes.h) with the entries of the papers ontology, which types.sh compiles into the non-packaged
 * data folder of the user (where the registry reads them like those of a package). Needs no server. Haiku only.
 */

#include <sen/SenTypes.h>
#include <sen/SenOntoPapers.h>

#include <string.h>

#include "TestHarness.h"

static bool
Has(const std::vector<std::string>& list, const char* wanted)
{
	return std::find(list.begin(), list.end(), wanted) != list.end();
}

TEST(TheTypesOfAnOntologyAreInTheRegistry)
{
	sen::types::Registry registry;
	const sen::types::Type* paper = registry.Find(sen::onto::papers::type::kScientificPaper);
	CHECK(paper != NULL);
	if (paper == NULL)
		return;
	CHECK_STR(paper->title, "Scientific Paper");
	CHECK_STR(paper->ontology, "papers");
	CHECK(!paper->abstract);
	CHECK(Has(paper->conformsTo, "org.sen-labs.document"));
	CHECK(Has(paper->formats, "application/pdf"));
	CHECK(Has(paper->formats, "application/epub+zip"));

	const sen::types::Type* document = registry.Find(sen::onto::papers::type::kDocument);
	CHECK(document != NULL && document->abstract);
	CHECK(registry.Find("org.sen-labs.nothing") == NULL);
}

TEST(TypesConformToWhatTheyAreSpecialCasesOf)
{
	sen::types::Registry registry;
	const char* paper = sen::onto::papers::type::kScientificPaper;
	CHECK(registry.ConformsTo(paper, paper));
	CHECK(registry.ConformsTo(paper, "org.sen-labs.document"));
	CHECK(registry.ConformsTo(paper, "public.document"));       // (of another system: it ends the chain, the registry does not know it)
	CHECK(!registry.ConformsTo("org.sen-labs.document", paper));

	std::vector<std::string> ancestors = registry.Ancestors(paper);
	CHECK_EQ(ancestors.size(), 2u);
	if (ancestors.size() == 2) {
		CHECK_STR(ancestors[0], "org.sen-labs.document");        // nearest first
		CHECK_STR(ancestors[1], "public.document");
	}

	bool found = false;
	for (const sen::types::Type* type : registry.Descendants("org.sen-labs.document"))
		found = found || type->id == paper;
	CHECK(found);
}

TEST(TypesAreFoundByTheFormatOfTheirFiles)
{
	sen::types::Registry registry;
	bool pdf = false, epub = false, text = false;
	for (const sen::types::Type* type : registry.ForFormat("application/pdf"))
		pdf = pdf || type->id == sen::onto::papers::type::kScientificPaper;
	for (const sen::types::Type* type : registry.ForFormat("application/epub+zip"))
		epub = epub || type->id == sen::onto::papers::type::kScientificPaper;
	for (const sen::types::Type* type : registry.ForFormat("text/plain"))
		text = text || type->id == sen::onto::papers::type::kScientificPaper;
	CHECK(pdf && epub && !text);
	CHECK_EQ(registry.OfOntology("papers").size(), 2u);
}

TEST(TheAttributesOfATypeAreThoseOfAMimeTypeAndTheInheritedOnes)
{
	sen::types::Registry registry;
	BMessage info = registry.Attributes(sen::onto::papers::type::kScientificPaper);
	const char* name;
	bool doi = false;
	int32 count = 0;
	for (int32 index = 0; info.FindString("attr:name", index, &name) == B_OK; index++) {
		count++;
		if (strcmp(name, "dc:identifier") == 0) {
			doi = true;
			// the other fields are parallel arrays, as in the attribute info of a MIME type
			CHECK_STR(info.GetString("attr:public_name", index, ""), "DOI");
			CHECK(info.FindBool("SEN:searchable", index));
			CHECK_EQ(info.GetInt32("attr:type", index, 0), (int32) 'CSTR');
		}
	}
	CHECK(doi);
	CHECK(count >= 10);
	CHECK_STR(info.GetString("type", ""), sen::onto::papers::type::kScientificPaper);
}

TEST(TheMappingsToOtherVocabulariesAreThere)
{
	sen::types::Registry registry;
	const sen::types::Type* paper = registry.Find(sen::onto::papers::type::kScientificPaper);
	CHECK(paper != NULL);
	if (paper == NULL)
		return;
	bool exact = false;
	for (const sen::types::Mapping& mapping : paper->mappings) {
		if (mapping.predicate == "skos:exactMatch" && mapping.object == "schema:ScholarlyArticle") {
			exact = true;
			CHECK(!mapping.justification.empty());
			CHECK(!mapping.confidence.empty());
		}
	}
	CHECK(exact);
}

TEST_MAIN()
