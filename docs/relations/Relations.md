# How they work for the user

# How they work internally

# Types

## Manual / Static

## Automatic / Dynamic

Driven by queries, defined by abstract Relations, e.g. "same" or "similar".
Relations are then defined by affected attributes.

E.g. "same size" and "similar size":
* Relation source and target is bound to FileType `BEOS:TYPE=image/*`
* Relation attributes are bound to `Media:Width` and `Media:Height`
* source value for comparison is taken from relation source attribute
* on invocation, SEN builds a Query from the relation `formula` with the source dimensions and a range (for similarity)
  or exact value (for equality) for the respective target attributes
* for similarity, the matching value is calculated with a predefined fuzziness factor from
  the Relation config

So, for similar images, the relation is defined as:
* `childOf` = `<similarImage>`
* `name` = dimensions
* `formula` = `Media:Width <= {$src.Media:Width * conf:fuzzinessFactor} && Media:Height <= {$src.Media:Height * conf:fuzzinessFactor}`
* `conf:fuzzinessFactor` = 1.1

`childOf` is a `Meta Relation` and uses SEN to reference the parent relation config via a `childOf` relation, 
which is explained further down below.

The `formula` configuration needs some explanation:
* a formula is similar to a `Query` but can contain placeholders and simple calculations, which native `BQueries` cannot
* special features not supported in native Queries are defined in blocks enclosed by curly brackets `{}`
* `$src` is a placeholder for the relations source, followed by an attribute name
* the similarity is configured by a tolerance percentage in `conf:fuzzinessFactor`
* on invocation, SEN resolves `$src.Media:Width` to the value of the corresponding attribute in the source file,
  and performs the calculation defined regarding the tolerance factor, inserting the literal result into the Query
  that is then executed as a native `BQuery`.

Lastly, the `similarImage` relation referenced as parent above is defined as:
* `childOf` = `similar` (the base relation)
* `query` = `BEOS:TYPE=image/*` (base query, inherited, AND'ed together)
* `name` = image

When resolving the relation, SEN walks the hierarchy and since the query configuration is inherited,
we automatically get a combined query like this (given a HD image with 1920x1080 pixels and a `fuzzinessFactor` of `1.1`):

```
((Media:Width<=1900) && (Media:Width<1960)&&(BEOS:TYPE=="image/*"))
```
