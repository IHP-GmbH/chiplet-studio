# intm4tm2.stackup.yaml

Verbatim copy of `libs.tech/chiplet_studio/intm4tm2.stackup.yaml` from
**IHP-GmbH/OpenIntM4TM2**, identified by its sha256 below rather than by a
commit. To update it, re-copy that file from the interposer PDK and change the
sha256 here and `kPdkStackupCopySha` in `tests/test_tech_resolve.cpp`.

    sha256  d4111b96a0224b5b90f96a0e4417dd596b019c790ae8860147eac55cff2b29e6

Studio keeps drawing intm4tm2 with its bundled `configs/stackups/intm4tm2.yaml`,
which takes the key's z and height for every layer this copy has. This copy is
here so the suite notices when the two drift apart.

They agree on every layer this file has (key, purpose, z, height) and on the
attach plane, 10.8103 um. Names differ by convention only: `X.filler` here is
`XFiller` there, and `X.pillar` is `X_pillar`. The bundled file also draws
Passiv 9/0 and marks it `purpose: render_only`. The key now carries the
passivation (`beol.passivation`), but this copy leaves it out: Passiv holds the
openings in the film, not the film, so an extruded entry would draw the holes.
Neither file models BackMetal1.

`TechResolve.PdkStackupFixtureAgreesWithBundledOnEverySharedLayer` asserts the
hash and the agreement above. Do not edit the copy here: change it in the PDK,
re-copy it and update the hash.
