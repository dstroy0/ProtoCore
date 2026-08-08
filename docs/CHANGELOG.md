# Changelog

All notable changes to ProtoCore are documented here.

## [Unreleased]

### Bug Fixes

- select_envs resolved affected_common under the repo root, which the tools/ move broke ([`9b2cfc9`](https://github.com/dstroy0/ProtoCore/commit/9b2cfc901f33be726688ea77d2fdda35b2a504f5))

### CI / Build

- update CHANGELOG.md [skip ci] ([`61a0978`](https://github.com/dstroy0/ProtoCore/commit/61a09783a4537584a7edce46776a76b138d8d65b))

### Changes

- Merge branch 'main' of https://github.com/dstroy0/ProtoCore ([`a7b482b`](https://github.com/dstroy0/ProtoCore/commit/a7b482ba8f1299aa11891e77156ab60a233fba4a))

### Documentation

- regenerate feature tables + configurator + build_opt.h + example index [skip ci] ([`bccef65`](https://github.com/dstroy0/ProtoCore/commit/bccef653ff85aea8200a43f466173c3340ceaa25))

## [1.0.7] - 2026-08-08

<details>
<summary><b>Show Changelog for version 1.0.7 - 2026-08-08</b></summary>

### CI / Build

- update CHANGELOG.md [skip ci] ([`61a0978`](https://github.com/dstroy0/ProtoCore/commit/61a09783a4537584a7edce46776a76b138d8d65b))

### Changes

- Merge branch 'main' of https://github.com/dstroy0/ProtoCore ([`a7b482b`](https://github.com/dstroy0/ProtoCore/commit/a7b482ba8f1299aa11891e77156ab60a233fba4a))

### Documentation

- regenerate feature tables + configurator + build_opt.h + example index [skip ci] ([`bccef65`](https://github.com/dstroy0/ProtoCore/commit/bccef653ff85aea8200a43f466173c3340ceaa25))

## [1.0.7] - 2026-08-08

<details>
<summary><b>Show Changelog for version 1.0.7 - 2026-08-08</b></summary>

### CI / Build

- bump github/codeql-action from 4.37.4 to 4.37.6 ([`3becdef`](https://github.com/dstroy0/ProtoCore/commit/3becdef912fa08ef5843b95f41bb0669872830d5))

### Changes

- Bump version: 1.0.6 → 1.0.7 ([`dc77779`](https://github.com/dstroy0/ProtoCore/commit/dc7777905300cf86b75964b4f37d0b50fe458ef4))
- Merge Dependabot #25: build(deps): bump github/codeql-action from 4.37.4 to 4.37.6 ([`fcc991f`](https://github.com/dstroy0/ProtoCore/commit/fcc991f1d11b05ae146f011840320e2a93d4cf78))
- Merge branch 'c11-target' into main ([`1810f10`](https://github.com/dstroy0/ProtoCore/commit/1810f10f3c7f1ccf8ac810132053d4fcfcb5a1b1))

</details>

## [1.0.6] - 2026-08-08

<details>
<summary><b>Show Changelog for version 1.0.6 - 2026-08-08</b></summary>

### Changes

- Bump version: 1.0.5 → 1.0.6 ([`000a002`](https://github.com/dstroy0/ProtoCore/commit/000a002ef88aa779762b2a2dec08341ff0995919))

### Features

- the SSH host key comes from NVS, and TLS gets a software record layer ([`a476eb7`](https://github.com/dstroy0/ProtoCore/commit/a476eb755669d36fcabf4a0297faadbaf0383427))

### Refactor

- every backend pair selects on its capability, not on the build ([`22d4e53`](https://github.com/dstroy0/ProtoCore/commit/22d4e53a02e8d29924e166162cfec7d490072cd2))
- the RSA backends select on the capability, not on the build ([`5184397`](https://github.com/dstroy0/ProtoCore/commit/518439782922de663fc65c0bcc891b635f439923))

</details>

## [1.0.5] - 2026-08-08

<details>
<summary><b>Show Changelog for version 1.0.5 - 2026-08-08</b></summary>

### Bug Fixes

- every buffer in these modules is borrowed, and ban 19 has no waiver ([`ce65d0a`](https://github.com/dstroy0/ProtoCore/commit/ce65d0adb09c1ada4813ab0edb2191b8ae1c9446))

### Changes

- Bump version: 1.0.4 → 1.0.5 ([`6d9549a`](https://github.com/dstroy0/ProtoCore/commit/6d9549a1a38411f1adc85395008a713322c7f616))

### Features

- a portable SNTP client, so the wall clock exists off the SDK's ([`d8ef1ae`](https://github.com/dstroy0/ProtoCore/commit/d8ef1ae10b629c6e8c3b98b37f000ec78641c815))

</details>

## [1.0.4] - 2026-08-07

<details>
<summary><b>Show Changelog for version 1.0.4 - 2026-08-07</b></summary>

### Bug Fixes

- three shell scripts resolved the repo root one directory short ([`cbb581f`](https://github.com/dstroy0/ProtoCore/commit/cbb581f88702305c2b9a3d453846220462e53786))

### Changes

- Bump version: 1.0.3 → 1.0.4 ([`3ede876`](https://github.com/dstroy0/ProtoCore/commit/3ede876c6c897e213dd033c130f93e45b93eea16))

### Features

- a portable responder, so mDNS exists on a part with no vendor component ([`5650016`](https://github.com/dstroy0/ProtoCore/commit/5650016d0b0dec2500eed0a816a97492769304d7))
- one DNS name codec, and dns_server reads names through it ([`2fcff15`](https://github.com/dstroy0/ProtoCore/commit/2fcff1588e2c00b627ed064a0704137643e97ac2))

### Refactor

- one path, and the UDP bind is covered on the wire ([`29518f7`](https://github.com/dstroy0/ProtoCore/commit/29518f723da3430386422cae0378d9208296fd26))

</details>

## [1.0.3] - 2026-08-07

<details>
<summary><b>Show Changelog for version 1.0.3 - 2026-08-07</b></summary>

### Bug Fixes

- the QUIC server closes its port, and its tests drive the real listener ([`f885604`](https://github.com/dstroy0/ProtoCore/commit/f8856044caee60b811f91b301a8783bcfaa57bb7))
- sequence the finish before the strlen it is compared against ([`1eb34c4`](https://github.com/dstroy0/ProtoCore/commit/1eb34c46ec2e0543c48272a3883da77f51bf0570))
- build_src_filter reaches core_setup with a step up ([`9101e43`](https://github.com/dstroy0/ProtoCore/commit/9101e43eac3966a4bc92b21cfe50e688017b057c))
- TcpListener bound add to stop's slot - positional init against a reordered struct ([`c16e6d8`](https://github.com/dstroy0/ProtoCore/commit/c16e6d8e3092ef57f833e176d6d0cee3dbe90810))
- put the repo root on the include path so core_setup/ resolves from src/ ([`04bbc72`](https://github.com/dstroy0/ProtoCore/commit/04bbc7250f27fbe291cafec662ce79ce9a8a64b0))
- repoint every board_drivers/ include at core_setup/, which the move left dead ([`8ee96f8`](https://github.com/dstroy0/ProtoCore/commit/8ee96f805e0c889a5ead944ca43d45e714b437d7))
- the crypto vector generators resolved test/ relative to their own dir, which the move broke ([`f64434d`](https://github.com/dstroy0/ProtoCore/commit/f64434d9b4329d75d895f1c88467b2030157f3d9))
- drop baseline entries for core_setup, which is not source ([`12913d8`](https://github.com/dstroy0/ProtoCore/commit/12913d833a717d7c1e4b16b7eff45a60fecd146f))
- two generators could not run, and three wrote the tree when asked to check it ([`1b2dadf`](https://github.com/dstroy0/ProtoCore/commit/1b2dadfce4fc0352c3c4d8b924003341abc11c0a))
- the paths the move left behind - tools/crypto, core_setup, and the report the two runners disagreed on ([`baae144`](https://github.com/dstroy0/ProtoCore/commit/baae144b4d55dfdd4a5c10ede1a7f9e5b227eed8))
- rx_feed include landed inside a feature guard in two suites ([`552dfe0`](https://github.com/dstroy0/ProtoCore/commit/552dfe0e2b7ea9692824cbc5f82939120a15938e))
- unfuck the runners' hook and quic_tls's two dead relative includes ([`6214032`](https://github.com/dstroy0/ProtoCore/commit/621403252fec7f538cebe1acd39e5378fa6af356))
- the runners were reading Unity output for a tree that no longer exists ([`3f142fe`](https://github.com/dstroy0/ProtoCore/commit/3f142fe93bc5594fc736d080dcbd7d82ded4b898))
- a checker that scans nothing was exiting 0 ([`31c2342`](https://github.com/dstroy0/ProtoCore/commit/31c23426213c113f4d0c9cf8bfe757a75b4be70d))
- the code behind arms nothing compiles, which a rename rotted ([`ca2cf36`](https://github.com/dstroy0/ProtoCore/commit/ca2cf36e34df5539b7e6bf033905e8f3bc3c8430))
- the third wake site, which no build in the matrix compiles ([`4278260`](https://github.com/dstroy0/ProtoCore/commit/42782609926e4d4d705d4a5d7453f126486150fa))
- the target build of the tcp transport, which reached through the session join for a table it already had ([`64934ac`](https://github.com/dstroy0/ProtoCore/commit/64934acb80f6ea3191543fe6337bd9dace77bfad))
- each ssh table lands after its declaration region, and children before parents ([`4b57ef0`](https://github.com/dstroy0/ProtoCore/commit/4b57ef0f944f2619d7590b3929bede5933379149))
- rng's envs link protomem, and test_ssh_pqc finds the KAT the restructure moved ([`be24170`](https://github.com/dstroy0/ProtoCore/commit/be24170db65993ac103794c998edda3ba2c92f9d))
- dtls_conn forward-declares the one function it calls above its definition ([`459363f`](https://github.com/dstroy0/ProtoCore/commit/459363f38eb51638b7b234f0ab1c59990ce8f326))
- the OAuth2 form-body builder passed the address of its own parameter ([`771b103`](https://github.com/dstroy0/ProtoCore/commit/771b103f3f7a6f7f5f389ac26ade6d0592835c61))
- base64.h takes protocore_config.h, the entry point that sets the widths ([`8635bf3`](https://github.com/dstroy0/ProtoCore/commit/8635bf34cb88204643399406d10acce385a75c02))
- the workers suite names Workers, not the layer above it ([`6101f95`](https://github.com/dstroy0/ProtoCore/commit/6101f9567f8eb8d10bcc9980ae3382bcd80594cb))
- a module's own policy list and suite name the module, not the layer root ([`c66cb41`](https://github.com/dstroy0/ProtoCore/commit/c66cb4190c8ce9477b7e529b1b3641ba5e71776c))
- the preempt-queue header order, and two TUs naming Session without it ([`e485fb0`](https://github.com/dstroy0/ProtoCore/commit/e485fb07188d921c3070aec4f2c24d9f572333c8))
- Physical carries the interface registry it was silently handing the radio ([`889c247`](https://github.com/dstroy0/ProtoCore/commit/889c247f9381ee72e5cb2e9bff0f20f909b8a445))
- forward reset no longer clears a table it does not own ([`dc441ec`](https://github.com/dstroy0/ProtoCore/commit/dc441eceda7bc13cec503559033e8ab661005031))
- restore the host busy_hold and busy_release my ps_get removal ate ([`1202db5`](https://github.com/dstroy0/ProtoCore/commit/1202db5c157b55677d2e41c585f99489c17d5f2d))
- physical is a layer, not a vendor seam, and carries eth and ip6 unconditionally ([`4037f0a`](https://github.com/dstroy0/ProtoCore/commit/4037f0aa5d6ac3bd30703c18f7b850611b2410e2))
- physical.h names RadioNs instead of including it, so the dependency runs one way ([`c691be3`](https://github.com/dstroy0/ProtoCore/commit/c691be36ac2c9052c747eedbb08aa90eb0fd69a7))
- forward-declare the two close helpers and take the join header in the listener ([`4f6ea1b`](https://github.com/dstroy0/ProtoCore/commit/4f6ea1bf7baa830af61f4a85446ba82fcc8f0c88))
- forward-declare the two teardowns and take the join header for Tcp ([`8372f26`](https://github.com/dstroy0/ProtoCore/commit/8372f26aeb99bfc6af4495311b450e89a4aeb274))
- forward.h drops the flat declarations its definitions no longer match ([`e01684c`](https://github.com/dstroy0/ProtoCore/commit/e01684c29ad31c15b24b5a22b076032dd29eedd0))
- the generators and the CI drift gate follow the suites into the new tree ([`02eff20`](https://github.com/dstroy0/ProtoCore/commit/02eff20ea94db17b277f58f5475922072076eec7))
- a stack base is a section, not an env, so nothing has to ignore tests ([`1fc7f09`](https://github.com/dstroy0/ProtoCore/commit/1fc7f0994c624da9a94f2be31f6b9843eab620b8))
- send_text measures its body with str.len, not a runops scan ([`69f7d6d`](https://github.com/dstroy0/ProtoCore/commit/69f7d6d3b398a3ac38d80fa87c9bd4657a72ec49))
- response.c takes the runops include send_text needs ([`ae31c0d`](https://github.com/dstroy0/ProtoCore/commit/ae31c0d879926dd78df6f190c1825b5f881c4ad8))
- the guards the dispatch-chain move left behind in protocore.c ([`e1b59c8`](https://github.com/dstroy0/ProtoCore/commit/e1b59c85d3c63ee74d4aa0efd63a61b49a5b3751))
- the env that builds protocore.c builds the mount registry it now resets ([`4a910ea`](https://github.com/dstroy0/ProtoCore/commit/4a910eac931fe2e9d26ff1485a44bb7294315852))
- the mount-point table empties with the routes it is indexed from ([`537e6bf`](https://github.com/dstroy0/ProtoCore/commit/537e6bf3e4a11aa1113176b776bed4336c137622))
- the credential table empties with the routes it is indexed from ([`b728733`](https://github.com/dstroy0/ProtoCore/commit/b728733b3be8de9fa5851593fe1e14eb68d503e9))
- the two long-lived tables take the arena end no mark walks, and declare their span ([`c601ae9`](https://github.com/dstroy0/ProtoCore/commit/c601ae95f193772a0591a14dbdfb36812ef8c731))
- the route table borrowed from an init nothing called, so every registration failed ([`943d1ba`](https://github.com/dstroy0/ProtoCore/commit/943d1ba261562bfe94d48227d93e95dd156d0254))
- the presentation envs build protostr, which the layer's string ops come through ([`3c79ba6`](https://github.com/dstroy0/ProtoCore/commit/3c79ba6f3259e190f8adb316cfa5fc4118a58479))
- presentation.c includes runops.h for the header scan, and the route envs build protomem.c ([`d84f016`](https://github.com/dstroy0/ProtoCore/commit/d84f01652aa858233708848c7e97edf8b8a89bcc))
- every env that builds protocore.c builds the network table it reaches through ([`9ffa670`](https://github.com/dstroy0/ProtoCore/commit/9ffa670da516014496bc07bb03e4687eb29f49e8))
- native_stack_http never built network.c, whose network table protocore.c reaches through ([`14e3ae3`](https://github.com/dstroy0/ProtoCore/commit/14e3ae3a8e93b9c3b7df4860b66e5f7b3e82d0ab))
- auth.c closed its feature guard before check() and the Auth table, which need it ([`1122998`](https://github.com/dstroy0/ProtoCore/commit/11229983a4a4f68ba55f3e737570a91225cf6a6a))
- native_client never built ip.c, whose Ip table the listener's allowlist matches through ([`bf4d521`](https://github.com/dstroy0/ProtoCore/commit/bf4d521a46d9ec0cd31138c10c33895222367e8f))
- the worker-queue members gate with PC_WORKER_COUNT, and native_client builds the join ([`f348ce3`](https://github.com/dstroy0/ProtoCore/commit/f348ce34a19b46fb22660e92f8d8ddf24e1c4dae))
- an env that builds the Tcp join builds all three halves it names ([`9d4ecfa`](https://github.com/dstroy0/ProtoCore/commit/9d4ecfa4ac42d54dbf613e704943ad1690cc8cfc))
- the moved tcp modules reach their siblings, and the observability trio gates with its flag ([`a022535`](https://github.com/dstroy0/ProtoCore/commit/a022535033e4e16cb14ea4f338b08527d0fa5cf4))
- the moved tcp headers reach tcp_evt.h one directory up ([`e983163`](https://github.com/dstroy0/ProtoCore/commit/e98316312e876c33ac37ad1663c9c88c31611207))
- rebinding a bound udp port reuses its slot, and the four suites assert the ring contract ([`29e4220`](https://github.com/dstroy0/ProtoCore/commit/29e4220c6c5fd419a58c693c96169937fbc1924b))
- pc_ip tags its family in .family, not .type ([`cf58a0c`](https://github.com/dstroy0/ProtoCore/commit/cf58a0c3437277a6841e9a4d005f72454e9ec2ff))
- the udp envs never built ip.c, whose Ip table the halves parse and format through ([`9422e02`](https://github.com/dstroy0/ProtoCore/commit/9422e0244c3778f41ec503e8b3d09ed70f216904))
- native_stack_http never built protomem.c, so its 25 children failed to link ([`73f0496`](https://github.com/dstroy0/ProtoCore/commit/73f0496af415ee8d0577590cf026c16c2aa77244))
- the two include breaks a full-matrix run turned up, and the stack bases it ran by mistake ([`0278e26`](https://github.com/dstroy0/ProtoCore/commit/0278e26259ad945b5da2aa16f42e81af86f1ed6a))
- the base flags that defeated PROTOCORE_HOT_FORCE, so the target path gets a test env ([`8a7a743`](https://github.com/dstroy0/ProtoCore/commit/8a7a743d43ab2d18d7bb994a68df8c5918ec6377))
- the dns server takes the raw mover's header, not the span module's ([`7ec4a3a`](https://github.com/dstroy0/ProtoCore/commit/7ec4a3ab7b40662f287058009d1e8254d8e3842e))

### CI / Build

- one example-discovery action instead of the same 29 lines in two workflows ([`f19b5b5`](https://github.com/dstroy0/ProtoCore/commit/f19b5b5cc882176324c959ab30f0389c547eaa3f))
- build the platform the project ships on, and pull before committing to a tree CI writes ([`3b0d362`](https://github.com/dstroy0/ProtoCore/commit/3b0d362b1ff828265b50999f595b0ac6c3b8c131))
- drop the scratch bucket map that rode along with the test move ([`300d62d`](https://github.com/dstroy0/ProtoCore/commit/300d62d066753cb8592d7055e0e9359d26ccc702))
- the byte ops and the libc string/stdio includes become enforced bans ([`beadbf0`](https://github.com/dstroy0/ProtoCore/commit/beadbf0d23e7e932b40bb0c80ce791f1adb41c45))
- the exec bit on the 16 shell scripts that carry a shebang and never had one ([`aee9c25`](https://github.com/dstroy0/ProtoCore/commit/aee9c25589ceedc83d152bd17586beb4a3f6bae7))

### Changes

- Bump version: 1.0.2 → 1.0.3 ([`7e1854b`](https://github.com/dstroy0/ProtoCore/commit/7e1854bdc7ba97700851f844916f463d379daee5))
- clang-format the tree, and pin the one macro run it cannot converge on ([`c2d5301`](https://github.com/dstroy0/ProtoCore/commit/c2d5301458f708258f8a0d8830ea606e76d011af))
- clang-format the two rig sketches, and the hook's own package invocation ([`13e8bda`](https://github.com/dstroy0/ProtoCore/commit/13e8bda42200731e8a2ba99fa93b50950e17e4ec))
- back out the ssh table conversion until it carries per-member feature guards ([`8b329ff`](https://github.com/dstroy0/ProtoCore/commit/8b329ffd747c46e144273bf3666fe7a1a9f13983))
- Revert "refactor: the listener registry moves to the listener that owns the pool" ([`a4e4145`](https://github.com/dstroy0/ProtoCore/commit/a4e4145cc06f70c4026bdcd328288e7a49577b42))

### Documentation

- name the crypto arms HW path and SW path, and stop calling the fallback a test path ([`12d88ab`](https://github.com/dstroy0/ProtoCore/commit/12d88ab464e38bc5a318701e7a459d69d9cb07fc))
- stop sending readers to hardware for what the host now covers ([`447abcf`](https://github.com/dstroy0/ProtoCore/commit/447abcf81e82f2e8b64a6cfbeb547116bb0f4874))
- log the fieldbus audit findings incl. a verified out-of-bounds axis read ([`81bb6ff`](https://github.com/dstroy0/ProtoCore/commit/81bb6ffd920cae1aedb1a75ffcc7220e964e1a73))
- log the remaining audit findings, all ten scopes now reported ([`d214d2d`](https://github.com/dstroy0/ProtoCore/commit/d214d2d69ce8f39062ed2bbf6a090fd9b78d0e12))
- log the security, storage and transport audit findings ([`a851419`](https://github.com/dstroy0/ProtoCore/commit/a851419e13a41fd5a8ded55a983402074389f951))
- log the QUIC and HTTP/3 conformance findings ([`fa1e87b`](https://github.com/dstroy0/ProtoCore/commit/fa1e87b30ae664e4295e0ad37fd6377d34390080))
- log the audit findings so far, and move the forwarding plane to L3 ([`22b3490`](https://github.com/dstroy0/ProtoCore/commit/22b34907157b99a2da6408859cd7f247efa550fc))
- log the HTTP/2 spec violations and the untested request bridge ([`798767f`](https://github.com/dstroy0/ProtoCore/commit/798767f006c0eb3f0e41c561d732a3f23a68a43b))
- the runner comment matches how a base is emitted ([`ed3fec2`](https://github.com/dstroy0/ProtoCore/commit/ed3fec25d8fe3e9fe6ae77ed8174ad97fe65bfd4))
- drop the BUGS.md entry for a defect I introduced and logged ([`8639666`](https://github.com/dstroy0/ProtoCore/commit/86396660691adfb887c035929ae45df1adb83d56))
- SRCBANNED is the ban list ([`b260cac`](https://github.com/dstroy0/ProtoCore/commit/b260cac5b86331c9fa4e827620ae598e0cfefe9a))
- stdio is banned, and every header path in SRCBANNED now resolves ([`2b1715f`](https://github.com/dstroy0/ProtoCore/commit/2b1715f30d4ba722627c001303a09623b4ed4122))
- cut the three law docs by 55 percent, and correct the type-name row ([`9cfc4f8`](https://github.com/dstroy0/ProtoCore/commit/9cfc4f87f5534aa16d564078ea234e388d5c8b3d))
- log the arena sizing and id-table reset defects ([`c881d70`](https://github.com/dstroy0/ProtoCore/commit/c881d70ffd89df78efe676c7e103d41b61cca6c8))

### Features

- float_bits.h, and membuild reads IEEE-754 fields through it ([`8d5acf4`](https://github.com/dstroy0/ProtoCore/commit/8d5acf473921101cd80a4a510850bdacc0bf93d0))
- the ring answers which slot, and what a held one keeps out ([`6498e32`](https://github.com/dstroy0/ProtoCore/commit/6498e32e946cd9c5d84e8f3282167371b82a464b))

### Refactor

- one path through the SSH client, the H3 RNG, and the packet framer ([`0f343c1`](https://github.com/dstroy0/ProtoCore/commit/0f343c17645c17359c59a0e981788b6e04a7dc9e))
- one path, and the DMA lane runs off the DMA driver ([`aaa2547`](https://github.com/dstroy0/ProtoCore/commit/aaa25471f5713a79331fe1498231c0675854e1dd))
- one path for the worker tasks, and designate the table ([`6858153`](https://github.com/dstroy0/ProtoCore/commit/6858153c9bdb85213ab8a9859e6789c67025aa34))
- one path, the redundant host arm is gone ([`9349a6e`](https://github.com/dstroy0/ProtoCore/commit/9349a6ee9fdea7f14c45408880bfa068b93a6979))
- size the outbound path from the secure pool ([`dae9b42`](https://github.com/dstroy0/ProtoCore/commit/dae9b426182391e0046fb73688baf06f33784050))
- designate the namespace tables, and cover net egress ([`fda58d0`](https://github.com/dstroy0/ProtoCore/commit/fda58d05c89cd1640aee08a90994eaae11f72473))
- designate UdpListener's initializer ([`ac33d37`](https://github.com/dstroy0/ProtoCore/commit/ac33d37a0ef6ef11079917bc2ee72baadccfb4a2))
- one slot allocator - tcp's free_mask and udp's linear scan are pc_slot_* ([`9069585`](https://github.com/dstroy0/ProtoCore/commit/90695855dc3c3e4681d6038697d071a57967045e))
- one CCOUNT measurement, and the rig build scripts find the repo again ([`c6ae4a2`](https://github.com/dstroy0/ProtoCore/commit/c6ae4a2f58e54b0b7fec33229d4caa2f15c9f405))
- one repo_root, and gen_theme_blobs imports gen_themes instead of exec'ing it ([`8e0b9b1`](https://github.com/dstroy0/ProtoCore/commit/8e0b9b1ae9ad2c0e27a2d451c21c886d65766472))
- board_drivers out of src/, the benching trees into test/, ci_tooling under tools/ ([`3e59ead`](https://github.com/dstroy0/ProtoCore/commit/3e59ead69fd26692d46598bf8eafd08c2134f714))
- one rx-feed helper instead of 24 private copies of the same loop ([`8efdfdd`](https://github.com/dstroy0/ProtoCore/commit/8efdfdddf91ecbbca84629a7b1c7e2be8622e51a))
- one package, so the tools import each other instead of guessing ([`12af671`](https://github.com/dstroy0/ProtoCore/commit/12af6717d43482e417069a6a695bca11cb88d7c5))
- name the dns files after their subject, which their guards already did ([`b141a61`](https://github.com/dstroy0/ProtoCore/commit/b141a612ce4d472e44e8e850ca6d5c5f92377425))
- one dma path, and the host drives the one the target takes ([`8c0eecb`](https://github.com/dstroy0/ProtoCore/commit/8c0eecbb03cab94d6dfabc75cec1fe8121dc21c9))
- each ssh layer names its own message numbers, and a ring index wraps with a mask ([`0425962`](https://github.com/dstroy0/ProtoCore/commit/04259628e63a0e398bb36ea4834dd05f0c0ff87b))
- ssh reaches its functions through tables cut along RFC 4251's layering ([`9951a49`](https://github.com/dstroy0/ProtoCore/commit/9951a493cdc7768b948fe712b5870c8eb49067c8))
- crypto owns the generator, and a caller only ever asks for bytes ([`b4d5399`](https://github.com/dstroy0/ProtoCore/commit/b4d539926528499cdea183f8bbd80a598d7304ca))
- telnet and the three dtls modules reach their functions through tables ([`3dddfe1`](https://github.com/dstroy0/ProtoCore/commit/3dddfe14fa5de5b31cce267a9d36e2e61cd3c8db))
- the codec sub-tree reaches its functions through per-module tables ([`a07e3b8`](https://github.com/dstroy0/ProtoCore/commit/a07e3b8234e189653cc85372e4cf61378a724def))
- base64 reaches its four functions through Base64, and gets its C linkage back ([`633285f`](https://github.com/dstroy0/ProtoCore/commit/633285fe25df3af212c1e1dabd58668dd45a2ff5))
- layer 5 gets its tables and its root ([`3fbefd2`](https://github.com/dstroy0/ProtoCore/commit/3fbefd2d87f4b9562fe073ba3053315fcaacb3cf))
- one enum for what an interface is and which one to match ([`f808fd1`](https://github.com/dstroy0/ProtoCore/commit/f808fd1ccc7e9df8bca6b6a8df5a43124c2bd3e9))
- the interface registry moves to L1, which is what puts bytes on the wire ([`3516c75`](https://github.com/dstroy0/ProtoCore/commit/3516c755c700c2dce488ecde0497aa75418c11a0))
- one enum for modem sleep ([`b97ec7e`](https://github.com/dstroy0/ProtoCore/commit/b97ec7e9c2cebe083e74c9a88669c1cfd1c0ebcf))
- every caller above L1 reaches the physical interfaces through the handle ([`369b676`](https://github.com/dstroy0/ProtoCore/commit/369b676b4eeddb5ce5aa83c3e8fac649a3442455))
- layer 1 gets the namespace struct a device interface should have ([`c57ea07`](https://github.com/dstroy0/ProtoCore/commit/c57ea07727a17860f346fcb4c8a62ddb68147e90))
- tcp_conn exports one symbol; alloc_free and sndbuf join the table ([`790408c`](https://github.com/dstroy0/ProtoCore/commit/790408cf9cb971e49d71b5f30f373453016df06d))
- tcp_listener exports one symbol; add and add_dynamic join the table ([`a43155e`](https://github.com/dstroy0/ProtoCore/commit/a43155e87683e0a9e7e9c50a936f253b7ac6fe49))
- the forwarding plane exports one symbol and the network layer carries it ([`af66f56`](https://github.com/dstroy0/ProtoCore/commit/af66f56476b91f76ef8713d940f0f24d3f30763c))
- tcp_client drops the flat surface no caller was using ([`d9337b9`](https://github.com/dstroy0/ProtoCore/commit/d9337b9bdaf01d2ee89d8d3114f682f7025d83f9))
- the per-connection and per-listener DSCP marks move to the objects that own them ([`276e5c5`](https://github.com/dstroy0/ProtoCore/commit/276e5c57652199553ab3ace42c335fb9b9febd2b))
- session stops re-declaring the presentation functions it does not call ([`6df2655`](https://github.com/dstroy0/ProtoCore/commit/6df265596d5108e890964125c8d054cce13472b2))
- the HTTP route table leaves the network layer and takes an HTTP name ([`4bfcef0`](https://github.com/dstroy0/ProtoCore/commit/4bfcef0066df7106fe7ccf9ebb738fc82abe8cc7))
- the HTTP/3 request bridge gets the TU its h2 twin has ([`619e880`](https://github.com/dstroy0/ProtoCore/commit/619e8804ab9196a57b199ea2c7de1c84652e0a82))
- the HTTP poll pump moves to the HTTP root ([`be71cfe`](https://github.com/dstroy0/ProtoCore/commit/be71cfe27f52bfc04e91256bf421e6073c31672b))
- the response senders move to server/response.c ([`32d062d`](https://github.com/dstroy0/ProtoCore/commit/32d062d190532be38a25eb5a652d9ea279d64b0a))
- the QUIC running flag goes back to the QUIC server ([`484ed74`](https://github.com/dstroy0/ProtoCore/commit/484ed74773cb2106dd2599a7cfa8a5cb417b5579))
- the listener registry moves to the listener that owns the pool ([`e85c964`](https://github.com/dstroy0/ProtoCore/commit/e85c964c3a8fb3b45b9c44fe7f52f88cea208cfc))
- auth and the no-match fallback move to the HTTP root ([`cee6e7e`](https://github.com/dstroy0/ProtoCore/commit/cee6e7ec19942c9efcc4a688fb66270a0ac5a722))
- the request dispatch chain moves to the HTTP root ([`4068015`](https://github.com/dstroy0/ProtoCore/commit/4068015a989fa9ffc3906eacdf490bf0a110a24e))
- HttpMethod is the presentation layer's, not the application layer's ([`c1a761d`](https://github.com/dstroy0/ProtoCore/commit/c1a761d951a758e6fd568d5e588bcf0cfce448c4))
- the HTTP root takes the version-agnostic surface out of the application layer ([`c10e60e`](https://github.com/dstroy0/ProtoCore/commit/c10e60ef2b08459167d3037cda5e0d23e09200bd))
- keep-alive is an HTTP/1.1 header rule, so it moves down to presentation beside its tally ([`52a8735`](https://github.com/dstroy0/ProtoCore/commit/52a87350eec122e1f9e0c5f60ec50194713b9f51))
- tcp splits into conn / listener / client under a Tcp join, and 865 call sites move ([`3efeee8`](https://github.com/dstroy0/ProtoCore/commit/3efeee852ee957eb70f70ac402ee01a9a25cccfa))
- the udp sends take an address, so a tag is serviced once by the service that owns it ([`2719548`](https://github.com/dstroy0/ProtoCore/commit/2719548aefaff83fed06110fd81d28379caa8d4a))
- udp.h becomes the join, and every caller moves onto Udp.listener / Udp.client ([`dabd807`](https://github.com/dstroy0/ProtoCore/commit/dabd8078f3ba9757a581a0ca09d02fdb8844415f))
- diffserv becomes a table ([`67f01c8`](https://github.com/dstroy0/ProtoCore/commit/67f01c896136289b7bee741296dcd393583ff5a2))
- ip is a shared primitive, not a network-layer module ([`75df9fd`](https://github.com/dstroy0/ProtoCore/commit/75df9fde10b9f5dfa9b596aef7fdee671111e356))
- ip becomes a table, reached through network ([`0c16338`](https://github.com/dstroy0/ProtoCore/commit/0c16338e5d1705beedcd562491126d6fbab47dbb))
- the dns server moves off implicit libc onto mem and str ([`0c3a467`](https://github.com/dstroy0/ProtoCore/commit/0c3a4676e445b199dda409076c1ba9e44aeb7365))
- dns joins under network; Route carries ids, not other modules' state ([`4e123fd`](https://github.com/dstroy0/ProtoCore/commit/4e123fdd63f47c0377b0a1b0eda708b558bc27f3))

### Testing

- unwind a task entry, and honor the queue depth create asked for ([`d23634c`](https://github.com/dstroy0/ProtoCore/commit/d23634c8ed071313332093de88442b0855f4a919))
- the host platform can run a started task ([`3563ce2`](https://github.com/dstroy0/ProtoCore/commit/3563ce23fb00d6e37089966ff7492a6caa7ea5d5))
- close two gaps in the DMA host mock ([`3babdc4`](https://github.com/dstroy0/ProtoCore/commit/3babdc445d3cb5aa8ea371b5bb27ad40142f5b27))
- per-file test groups under the owning layer ([`a766764`](https://github.com/dstroy0/ProtoCore/commit/a7667642ded014af0542d894f382adeb970c7333))
- a suite for the ring, and the shift it could not survive ([`39caf49`](https://github.com/dstroy0/ProtoCore/commit/39caf49975a04ea8ad64540ee54ae3f95b1f684c))
- give the multi-worker hot arm a witness ([`24097ea`](https://github.com/dstroy0/ProtoCore/commit/24097ea1d0c0ffd64fbe358fb96ddc1e3e8692fd))
- the host queue holds what is posted to it ([`f375c11`](https://github.com/dstroy0/ProtoCore/commit/f375c1121bb0278c92b16c8e804b2e619886f3f6))
- the L1 interface registry gets its own suite, and the dead forward sizing goes ([`14611df`](https://github.com/dstroy0/ProtoCore/commit/14611df28365015f00faca999cc9511533b6760b))
- native_forward links L1, the layer it now sends through ([`4708320`](https://github.com/dstroy0/ProtoCore/commit/4708320cd3a63f792c1f09f5e1b27d6faa159a41))
- native_net_egress links the radio the layer handle carries ([`b1bb3e8`](https://github.com/dstroy0/ProtoCore/commit/b1bb3e8db220c6cbbf244b736401625bfd1edd25))
- native_radio_power links the L1 core whose phy functions the radio table names ([`850301f`](https://github.com/dstroy0/ProtoCore/commit/850301fec2d2b093720a9a1173c94ece953ca8a9))
- native_tcp_hot links the arena that carries worker identity ([`6c50d36`](https://github.com/dstroy0/ProtoCore/commit/6c50d3628bbad083d1a1fceabff58f5580f39271))
- native_tcp_hot links the Tcp join and the worker its target arm calls ([`6d0a4e5`](https://github.com/dstroy0/ProtoCore/commit/6d0a4e5a5eed1d345d2ffba6890e3c265bc0f90e))
- native_tcp_hot drives the TCP target path through the mock harness ([`4021535`](https://github.com/dstroy0/ProtoCore/commit/4021535596c9bd1d7d725e0a65d44a178994e826))
- the runner finds pio in the venv both Linux envs install it to ([`266e436`](https://github.com/dstroy0/ProtoCore/commit/266e436810bd840cd2337bad0fab731faec01231))
- a protocol's crypto suite sits with the protocol, not under crypto ([`bab7762`](https://github.com/dstroy0/ProtoCore/commit/bab77625c3945f9b0efa134246558a59b68e2344))
- the suite tree sorts by what a test stands up, then by the module it covers ([`4d34ef3`](https://github.com/dstroy0/ProtoCore/commit/4d34ef3d254a72dc7ee0c6ba73c06feeb7c18207))
- a base env says it runs no suite instead of the runner blocklisting it ([`4b47961`](https://github.com/dstroy0/ProtoCore/commit/4b47961a0dee7b67f412b991caba0e1b92a1a65e))
- native_h3_server builds the bridge TU and calls it by its own name ([`e1b2ac0`](https://github.com/dstroy0/ProtoCore/commit/e1b2ac0988443fbd46ade27526311bbe4163e60a))
- the v3 trap test asserts the refusal the service actually makes ([`5a8a9ba`](https://github.com/dstroy0/ProtoCore/commit/5a8a9ba759faa75f01b5470df14d8ffc6811cbff))
- the suites hand the sends an address, and statsd is given one it can parse ([`7aff713`](https://github.com/dstroy0/ProtoCore/commit/7aff7134e18b17f4fa613c42a0a54b9d6f23dbb8))
- the client-side suites read the client capture, after the poll that writes it ([`49b318e`](https://github.com/dstroy0/ProtoCore/commit/49b318e9b2545bdf8bca82863834584aa7eb9e83))

</details>

## [1.0.2] - 2026-08-05

<details>
<summary><b>Show Changelog for version 1.0.2 - 2026-08-05</b></summary>

### Bug Fixes

- the four auth examples now enable PC_ENABLE_AUTH ([`2eaec7a`](https://github.com/dstroy0/ProtoCore/commit/2eaec7a12c0ed9e7a8ae9a8a83e3a5a51fc64b28))
- five examples still calling the C++ default-argument forms, and WebTerminal's missing dependency flag ([`98e6838`](https://github.com/dstroy0/ProtoCore/commit/98e683849b7fa4e3148f753dd7b8d3f17ff003dd))
- the coverage union aborted on a function with two build-time definitions ([`1dd0cc0`](https://github.com/dstroy0/ProtoCore/commit/1dd0cc0a44a9b7959ee0a778f9d1246b88671c8f))
- declare pc_ntp_http_date where it is defined, not behind PC_ENABLE_NTP ([`ad646a1`](https://github.com/dstroy0/ProtoCore/commit/ad646a1aa3990bde3ca744708da9243492034ee5))
- give the whole public API C linkage at the umbrella, not header by header ([`4605ea5`](https://github.com/dstroy0/ProtoCore/commit/4605ea55a9fbf9a89e8ffecc97f75ee6c7d588c4))
- extern C guards on the SHA headers ([`33fd865`](https://github.com/dstroy0/ProtoCore/commit/33fd865a7509bca41b2c567054979260bcc957b9))
- extern C guards on the five MAC and stream-cipher headers ([`27bc4b0`](https://github.com/dstroy0/ProtoCore/commit/27bc4b0cd5dcbc28f1454d255e7f220537dc833c))
- extern C guard on aes128gcm.h, and drop the std includes types.h already owns ([`75203df`](https://github.com/dstroy0/ProtoCore/commit/75203df623ac4d049651665b55cdb2585b5f0e87))
- three headers declared C functions with no extern C guard ([`36f572a`](https://github.com/dstroy0/ProtoCore/commit/36f572a031f8e582f5bb95d950318edbfd5d5f70))
- esp_aes128gcm named C++ alignof in a C11 file ([`6a82448`](https://github.com/dstroy0/ProtoCore/commit/6a82448766a810a4beab44b5d24521bcc1f2706f))
- esp_aes128gcm used C++ alignof in a C11 file ([`81e3039`](https://github.com/dstroy0/ProtoCore/commit/81e3039fc9999ca3f701c0c0205c9c4a086dab8c))
- the crypto bench's SecureScope use ([`fa68f69`](https://github.com/dstroy0/ProtoCore/commit/fa68f6924be5a2e428c05ac357bfa61d1ffdefa1))
- the crypto bench passes the DTLS record keys by pointer ([`2af2fb2`](https://github.com/dstroy0/ProtoCore/commit/2af2fb2e6e3b4445d4d1defbe62f67ae52250f8d))
- the crypto bench's hkdf_expand_label arity ([`7dae4a8`](https://github.com/dstroy0/ProtoCore/commit/7dae4a81ab8492f992e3a6b05d5c8df85a638f7f))
- the crypto bench's scoped-enum uses, left over from the C11 conversion ([`6237858`](https://github.com/dstroy0/ProtoCore/commit/6237858f9a82aa8422cfe3e9fb6566a8d50f153f))
- the crypto bench's stale mmgr include path ([`8bddf97`](https://github.com/dstroy0/ProtoCore/commit/8bddf97451f2f737a338b5130be975b350c55acb))
- bound the two RSA accelerator status polls ([`99ba4e2`](https://github.com/dstroy0/ProtoCore/commit/99ba4e2e292c03a959efb5498717463395337051))
- read the AES-GCM block through the raw accessors instead of a punned pointer ([`5b047bd`](https://github.com/dstroy0/ProtoCore/commit/5b047bd5fa4489eb424e6884f7b21ab27a69eef4))
- nine dropped status returns in the SFTP and file-serving paths ([`24a6f97`](https://github.com/dstroy0/ProtoCore/commit/24a6f973127426f50a7626679e2dad63a061038d))
- four signed-overflow parsers, an unbounded UART drain, and oidc's unwritten out-params ([`b8c11bf`](https://github.com/dstroy0/ProtoCore/commit/b8c11bffddbcc1bebc500e19709febc1a02798c8))
- link the NVS backend into the codeql coverage env ([`6c5a2f2`](https://github.com/dstroy0/ProtoCore/commit/6c5a2f2562e1ecda3ed367250aaf2d8f0f4398fb))
- two CI gates, the clang-format blind spot, and config_io's missing backend ([`f057523`](https://github.com/dstroy0/ProtoCore/commit/f057523207668c2e9b705d3cedf12011d7231c2e))
- run the peripheral drivers' real body wherever a bus seam exists ([`52418a6`](https://github.com/dstroy0/ProtoCore/commit/52418a68302826128b5fbe25f53e01fa34613f7c))
- route every timing call through the library clock ([`09e8d2b`](https://github.com/dstroy0/ProtoCore/commit/09e8d2b9dd85c5ece8a66b0c4e5071c7d3fe7263))
- include the header declaring pc_worker_set_self in worker.c ([`c0a548e`](https://github.com/dstroy0/ProtoCore/commit/c0a548e3b4ad6cf0619a820036850752506a63a1))
- read instruction bytes, and classify what each blob difference changes ([`652052d`](https://github.com/dstroy0/ProtoCore/commit/652052df7ffb3044a12d177ea3c35c78ed48d70f))
- give the ESP-NOW radio callbacks C internal linkage ([`ff4649e`](https://github.com/dstroy0/ProtoCore/commit/ff4649edfbf45b0f17c6a552c1104827b6316521))
- define the SSH client identification frame spec ([`1526bc4`](https://github.com/dstroy0/ProtoCore/commit/1526bc4d4c1bd1aaff33101a2c228c9f75881ebe))
- name every omitted parameter, and move the I2C drivers onto the bus owner ([`f3cf57d`](https://github.com/dstroy0/ProtoCore/commit/f3cf57dca53eb225f959c22e97a8e9172ee9bd68))
- the service headers give their declarations C linkage ([`375a461`](https://github.com/dstroy0/ProtoCore/commit/375a46164dc254004d652c26444e813e1f80773c))
- examples pointed at pre-migration header paths ([`d704047`](https://github.com/dstroy0/ProtoCore/commit/d7040474521240bd9dc7cc12a4b24fab10560b0c))
- the conversion left .cpp paths in the asset generator and two docs ([`fcf87ac`](https://github.com/dstroy0/ProtoCore/commit/fcf87ac8e6d13670879a8dcce8c6c7c4e98a6e17))

### CI / Build

- update test report + coverage [skip ci] ([`8c83fb4`](https://github.com/dstroy0/ProtoCore/commit/8c83fb43633f6449f7a6e93bd7c8fc097d413dec))
- update CHANGELOG.md [skip ci] ([`68d457c`](https://github.com/dstroy0/ProtoCore/commit/68d457cbfc97c130d0b000eb6488f3f9d0ae8fd2))
- rename the format workflow, since clang-format is one step of several ([`098c792`](https://github.com/dstroy0/ProtoCore/commit/098c792fc28064d64095588d26dfb4ee841fc1a7))
- gate Python formatting, and keep vendored components out of the C style sweep ([`225a20c`](https://github.com/dstroy0/ProtoCore/commit/225a20c2184840583c8e648516f07411fa991070))
- unblock the two formatting gates ([`8660430`](https://github.com/dstroy0/ProtoCore/commit/86604301072979c929252f2c3c53ac0cee34b1e9))
- update CHANGELOG.md [skip ci] ([`8a6e286`](https://github.com/dstroy0/ProtoCore/commit/8a6e286888f51f6db0c942df9cb799d964fb93d7))
- update test report + coverage [skip ci] ([`bbb0215`](https://github.com/dstroy0/ProtoCore/commit/bbb02150fdd194463e43615dbe7932a5598ccc49))
- update CHANGELOG.md [skip ci] ([`c247de8`](https://github.com/dstroy0/ProtoCore/commit/c247de8e010fb169d95742eac0b62e6aa8d7678c))
- move to pioarduino so the toolchain is current ([`3e1561d`](https://github.com/dstroy0/ProtoCore/commit/3e1561d9287ee7e412194c4432355f689e1c9d9a))
- unpin the espressif32 platform so the toolchain tracks latest ([`349cbf9`](https://github.com/dstroy0/ProtoCore/commit/349cbf9be4581565a483344071694b729e7016ea))
- the naming law stops demanding a C++ construct, and reads the enum's real name ([`0eee3a2`](https://github.com/dstroy0/ProtoCore/commit/0eee3a2c6ed6dc7a2ceb79e072d3e2b5981e2bec))

### Changes

- Bump version: 1.0.1 → 1.0.2 ([`cf46e12`](https://github.com/dstroy0/ProtoCore/commit/cf46e12721e634b80680771bda4b1dfdc494babe))
- Merge pull request #24 from dstroy0/c11-target ([`4d9b245`](https://github.com/dstroy0/ProtoCore/commit/4d9b2457562f378c3c27da78300393c9e7f2fbe9))
- Merge remote-tracking branch 'origin/main' into c11-target ([`a2a5fff`](https://github.com/dstroy0/ProtoCore/commit/a2a5fff60842e82fa7cb978a05eee75ac457d3e3))
- Merge pull request #23 from dstroy0/c11-target ([`3d74266`](https://github.com/dstroy0/ProtoCore/commit/3d74266b1d59f5f7492bf27af1f41180a02f7958))
- survey a JTAG DRAM dump for dispatch tables ([`744ca7f`](https://github.com/dstroy0/ProtoCore/commit/744ca7f3a62aacfbc6f752772f5a84bd6666aa53))

### Documentation

- the radio keep-awake note names Radio.busy_hold ([`afe2fcd`](https://github.com/dstroy0/ProtoCore/commit/afe2fcda2f31f8b647952c9f5bd3e91c52ec11ea))
- update ESP32 build footprints [skip ci] ([`2391111`](https://github.com/dstroy0/ProtoCore/commit/239111185a5b5812356651559e9901fd64234c64))
- regenerate feature tables + configurator + build_opt.h + example index [skip ci] ([`8ad23ae`](https://github.com/dstroy0/ProtoCore/commit/8ad23ae384ce2b203ffdb128423865693f34085e))
- regenerate what the two new feature flags feed, and name the stack idemIP ([`c9587e6`](https://github.com/dstroy0/ProtoCore/commit/c9587e60135f1dc674abbc23b8b515d84377b0d4))
- regenerate the README feature tables for SMBus and PMBus ([`5905026`](https://github.com/dstroy0/ProtoCore/commit/590502681bc0e295af698f92f9b822be283d74d7))
- the docs badge names ProtoCore, not the host it sits on ([`1ec8834`](https://github.com/dstroy0/ProtoCore/commit/1ec88344dee7cf34c8bad2c5882b9275d3df0ef7))
- state the namespace struct as the module's public surface ([`20bcec8`](https://github.com/dstroy0/ProtoCore/commit/20bcec88c1f7e8858ea69766d5f699219a200ce5))
- state the C11 object as the endorsed shape in ban 22, and add the bench that prices it ([`7fa7048`](https://github.com/dstroy0/ProtoCore/commit/7fa70481b6e923c7ba5831285d8694f275e6a183))
- update ESP32 build footprints [skip ci] ([`d3fde32`](https://github.com/dstroy0/ProtoCore/commit/d3fde3258a02d57be307f7a836816c6aa3574d46))
- record the SMBus and PMBus flags, and the third .cpp exemption ([`3a46de4`](https://github.com/dstroy0/ProtoCore/commit/3a46de4143e0c88f4ccce276fcaa22c47b1a521f))
- log three bugs the bus-owner work surfaced ([`a106b19`](https://github.com/dstroy0/ProtoCore/commit/a106b193e002e955f43713c15748b9eefc4d46f1))
- inventory the radio functions that must stay in IRAM ([`bdfef8e`](https://github.com/dstroy0/ProtoCore/commit/bdfef8e697c1be111a8ae09e500f307d9dc93d71))
- diff the radio blobs' code between installs, function by function ([`ebc29b6`](https://github.com/dstroy0/ProtoCore/commit/ebc29b6f0d627576404ed9a2b40da3c29653baa6))
- compare the radio blob symbols between the Arduino and IDF installs ([`8b92f56`](https://github.com/dstroy0/ProtoCore/commit/8b92f56ddd678746c3b80596c4bcbd4693bad5e2))
- extend radio blob parity to every ESP die IDF ships ([`3f565c0`](https://github.com/dstroy0/ProtoCore/commit/3f565c0ea14a2c3c6380468bfbcc55fafdc83890))
- cross-reference the radio blobs across every ESP variant ([`2969c14`](https://github.com/dstroy0/ProtoCore/commit/2969c1441e3f9d01390f825ee6526b97dd50b1a0))
- decode the analog bus primitive out of libphy's own iram1 ([`87a5ed2`](https://github.com/dstroy0/ProtoCore/commit/87a5ed27599a1e240407f6c52ef78a19e4972e5b))
- capture the analog RF programming sequences from the radio blobs ([`01f13e6`](https://github.com/dstroy0/ProtoCore/commit/01f13e61e24912d0c4e76ff04c86ff6e7e283949))
- map the radio blobs' registers by function, and roadmap our own stack ([`06e1168`](https://github.com/dstroy0/ProtoCore/commit/06e116876aa732e97db98d995a6a5d07844f0936))

### Features

- protomem, the byte-span module ([`a24ded6`](https://github.com/dstroy0/ProtoCore/commit/a24ded6694269ec21308787cc6907da7dfc1ee46))
- gate the comment law, and sweep the CRC history clause ([`f64a89f`](https://github.com/dstroy0/ProtoCore/commit/f64a89f2e00f5440e507d4940974d79c356105d2))
- per-transfer log with timestamps in the host bus capture ([`4b4e0a3`](https://github.com/dstroy0/ProtoCore/commit/4b4e0a301e66a7ef74aca82df553df3886fb7f70))
- record the wire on host builds, so driver output is testable end to end ([`af9fe85`](https://github.com/dstroy0/ProtoCore/commit/af9fe85441c08bc5d1eb7603f75828fa68feae65))
- capture the live PHY dispatch table off an ESP32-S3 ([`affc928`](https://github.com/dstroy0/ProtoCore/commit/affc9280a48c7afc26bddf8a87dba92ff9aa49b1))
- the full I2C and SPI master protocol behind the bus owners ([`411026f`](https://github.com/dstroy0/ProtoCore/commit/411026f8a1221fa12c1900a22435c72d0cdd2e92))
- a microsecond delay, measured on the raw counter ([`0ef3ae6`](https://github.com/dstroy0/ProtoCore/commit/0ef3ae60f865c2bb5adb8a3e6cd06313bde6ef0f))
- a microsecond delay beside the millisecond one ([`f33c51d`](https://github.com/dstroy0/ProtoCore/commit/f33c51d70a018b84dfbb5af401c767f3a1abc75c))
- a shared SPI bus owner beside the I2C one ([`f3f7e59`](https://github.com/dstroy0/ProtoCore/commit/f3f7e59e56f76974f93898710114ef8ac6f2b571))

### Refactor

- mmgr owns the byte layer; auth owns its credentials ([`a469bad`](https://github.com/dstroy0/ProtoCore/commit/a469bad40acad07d3340348911af385860862f40))
- the route table lives in the secure pool ([`3ef5c5a`](https://github.com/dstroy0/ProtoCore/commit/3ef5c5a508ce3c05bebe39f5c99c38da6fc57b7f))
- route exports one symbol, carried as network.route ([`b602404`](https://github.com/dstroy0/ProtoCore/commit/b602404e61834867ba2548be2dc9e83f2e87ece1))
- network exports one symbol, Network ([`3c36382`](https://github.com/dstroy0/ProtoCore/commit/3c36382b19157449fd4810ee4eab16686a0fedd7))
- roaming moves its bytes through mem ([`8179f74`](https://github.com/dstroy0/ProtoCore/commit/8179f740395bb98c71b03714222bb6f11386c747))
- roaming exports one symbol, Roam ([`2a41dc3`](https://github.com/dstroy0/ProtoCore/commit/2a41dc3ede270e23344a945906a39162508269e2))
- datalink exports one symbol, Datalink ([`ad3f5fd`](https://github.com/dstroy0/ProtoCore/commit/ad3f5fd45a8fa330662a439319c1c341807e4ce8))
- radio_power exports one symbol, Radio ([`7ee0de9`](https://github.com/dstroy0/ProtoCore/commit/7ee0de92b608b240f20faf507de553c8a977addb))
- fold five copies of the hex digit table onto PC_HEX_LOWER ([`9c0726a`](https://github.com/dstroy0/ProtoCore/commit/9c0726ada37ef9c4c0d8bb162946b4e308d7feff))
- move the last three drivers onto the bus owners ([`6e5116a`](https://github.com/dstroy0/ProtoCore/commit/6e5116a140e7a81ba4f2daaf71d53b84cd278c93))
- split the xtensa-only radio tools into their own subdirectory ([`0dd66cd`](https://github.com/dstroy0/ProtoCore/commit/0dd66cdd6656b47a97b5a5765d571da75a17e5fa))
- move the radio reverse-engineering tooling to reverse_engineering/esp32_mac ([`2d7b324`](https://github.com/dstroy0/ProtoCore/commit/2d7b32475c51dd45a4b38ecc9000f98497d467d7))
- the INA219 driver reaches the bus through the i2c owner ([`4cf6985`](https://github.com/dstroy0/ProtoCore/commit/4cf6985f2fda5020327630aa55cd88bd77011534))
- the DMA submit moves its span through proto_raw_read ([`4c2c420`](https://github.com/dstroy0/ProtoCore/commit/4c2c420b2d9c00e55677fde3bba43400df724dca))

### Testing

- pointer nesting instead of by value ([`3a57057`](https://github.com/dstroy0/ProtoCore/commit/3a57057d1201b6a8a6e89c467a85b0827cd1e2d7))
- does a layer object embed a module's struct by value across a TU ([`72e1e29`](https://github.com/dstroy0/ProtoCore/commit/72e1e29f89b28bd5729a6ac38f0cc47116ba08eb))
- roaming borrows its BTM hint instead of declaring it ([`79a31df`](https://github.com/dstroy0/ProtoCore/commit/79a31dfe53cff86b4372ee3f397753668fded7b2))
- native_roaming builds protomem ([`7a8881a`](https://github.com/dstroy0/ProtoCore/commit/7a8881a42907382922bea5a13a7bb0e646bdc39d))
- a consumer TU for the converted radio module ([`a8eb1be`](https://github.com/dstroy0/ProtoCore/commit/a8eb1beb3edd5db089626b96cfa97b1ba5581eee))
- keep the canary's build to the library and its own main ([`ed37c2f`](https://github.com/dstroy0/ProtoCore/commit/ed37c2f4bdc43d89c495d4db5c60a1740d564961))
- measure whether the namespace-struct fold survives a TU boundary ([`3cfd435`](https://github.com/dstroy0/ProtoCore/commit/3cfd4352550968c4dda2bc9e4669eaa2019d38c8))
- a C ESP-IDF canary for the conversion ([`339f235`](https://github.com/dstroy0/ProtoCore/commit/339f23584ea6d65b4c50fe06b8111c3467ea521f))
- price the namespace struct in C on the target toolchain ([`4628a54`](https://github.com/dstroy0/ProtoCore/commit/4628a543d55ff485ff3934cfdc8ff6967d600769))
- keep the bench leaves out of line so the strip is what gets measured ([`1525354`](https://github.com/dstroy0/ProtoCore/commit/152535472471b6d83321c6f2f571b77b01ce2a46))
- assert the INA219 wire output instead of a host refusal ([`0b3fc66`](https://github.com/dstroy0/ProtoCore/commit/0b3fc66b388e8f27ec061997124b0f323e7d1709))
- assert the drivers' wire output instead of a host refusal ([`dfa44ac`](https://github.com/dstroy0/ProtoCore/commit/dfa44ac74a68803ece33d10c48d038ec276ef995))
- assert the SMBus and PMBus wire output instead of a host refusal ([`8bc0806`](https://github.com/dstroy0/ProtoCore/commit/8bc0806a1ac2541ad86d84c0d90bc6b5292b457d))
- assert the INA219 register write per transaction, not across the stream ([`9d3db6c`](https://github.com/dstroy0/ProtoCore/commit/9d3db6c9a5994dcbca5f0cf8cd476668f52291ba))

</details>

## [1.0.1] - 2026-08-04

<details>
<summary><b>Show Changelog for version 1.0.1 - 2026-08-04</b></summary>

### Bug Fixes

- drop an unreachable release left behind the return in ssh_conn ([`c48abc0`](https://github.com/dstroy0/ProtoCore/commit/c48abc08e71ace0d7f02b8022b248e7e9d880ffc))
- the RSA HAL reaches the entry point for the widths it is written in ([`3824d55`](https://github.com/dstroy0/ProtoCore/commit/3824d55945c35bee2f96134f171a2f880460c2ab))
- protocore.h gives its declarations C linkage, so a C++ sketch can link them ([`0a5950c`](https://github.com/dstroy0/ProtoCore/commit/0a5950cf176754fb7932ac2046f83a003624513e))
- the last two bodies that inherited tcp.h now name it ([`14514c8`](https://github.com/dstroy0/ProtoCore/commit/14514c81b67eab6b73b9401cb5d3f80f2cd29ba5))
- the transport and session bodies include tcp.h themselves ([`289f442`](https://github.com/dstroy0/ProtoCore/commit/289f4423ff95973b180f2557e1bfd20eec1f5231))
- listener.h names the address type it uses instead of inheriting it ([`2266af4`](https://github.com/dstroy0/ProtoCore/commit/2266af48341156228ee82d89aa4086963f9b7518))
- examples spell the cfg argument the C API no longer defaults, and Mnt carries its root ([`f1c1abf`](https://github.com/dstroy0/ProtoCore/commit/f1c1abf245a932356b79a08bebd594cabca98dee))
- tls.h stubs had unnamed parameters and empty parameter lists ([`b62eb53`](https://github.com/dstroy0/ProtoCore/commit/b62eb53aa398bc081fb133ecb9016c8c01e05508))
- crypto headers reach the entry point, not stdint and a board header ([`fe142bf`](https://github.com/dstroy0/ProtoCore/commit/fe142bf5742f6d0435da02dc51b465819d09b7a1))
- crypto headers tested PROTOCORE_HOT before anything defined it ([`1b229f7`](https://github.com/dstroy0/ProtoCore/commit/1b229f708c580ba55a903e75424e61db8633e658))
- physical_esp is C++ (Arduino WiFi/ETH), so it is .cpp again ([`6bcb1fe`](https://github.com/dstroy0/ProtoCore/commit/6bcb1fe3fba892245153a0574e6c33aa135c9779))
- an OIDC arena leak that rejected every token, and two more lost scope guards ([`713d3a7`](https://github.com/dstroy0/ProtoCore/commit/713d3a7ad34e5de1109d509e06cab8b4bdb569c3))
- the last of the test/ residue, and three envs that never linked their own deps ([`fc0a924`](https://github.com/dstroy0/ProtoCore/commit/fc0a92455f1caa10d72cb10d914da00e61baf4c9))
- brace assignment, lambdas, and six more defaulted arguments in test/ ([`463ddae`](https://github.com/dstroy0/ProtoCore/commit/463ddaef3e2d5d2bfd6f1d2accb657ecaeaddb61))
- restore the ESP mount adapter's extension, and three gaps behind it ([`8472e1b`](https://github.com/dstroy0/ProtoCore/commit/8472e1b62690f9834432a46ae5f0a5b92a128d55))
- the scoped-enum residue in test/, resolved by the compiler not by guessing ([`a2d479a`](https://github.com/dstroy0/ProtoCore/commit/a2d479a075ef1cba7646687739e29a8d18758579))
- the C++ the native suites could not see, and the WAL's missing barrier ([`f38924b`](https://github.com/dstroy0/ProtoCore/commit/f38924b1a8ab7cae0058f64f777937c446842ed7))

### CI / Build

- gcovr unions the coverage tracefiles; merge_coverage.py is gone ([`0dd3fde`](https://github.com/dstroy0/ProtoCore/commit/0dd3fdecae339876666e4b404c7ac4e338ec4676))
- measure coverage over all of src, with nothing excluded ([`6cf03ba`](https://github.com/dstroy0/ProtoCore/commit/6cf03baa893019b7c69a0cc07fc397dbdc057f9d))
- esp32dev states the C standard src/ is written in ([`0a6b03e`](https://github.com/dstroy0/ProtoCore/commit/0a6b03e738984d3fe51958847c195c9afce59a8f))
- library.json states the C standard src/ is written in ([`53f4893`](https://github.com/dstroy0/ProtoCore/commit/53f48933df5f34af833b02718d0970fed6ebc9ec))
- update test report + coverage [skip ci] ([`3620178`](https://github.com/dstroy0/ProtoCore/commit/3620178aec096b69e01fdf4a5036ff52fbdede74))
- update CHANGELOG.md [skip ci] ([`3206307`](https://github.com/dstroy0/ProtoCore/commit/320630791cff9fa6a715da3698d163413e924879))
- update test report + coverage [skip ci] ([`a112b07`](https://github.com/dstroy0/ProtoCore/commit/a112b071a235722428998020931c226afb9025a7))
- update CHANGELOG.md [skip ci] ([`3c1873f`](https://github.com/dstroy0/ProtoCore/commit/3c1873f1afebddccd48c4e68b4ba20ba4e4359be))
- update CHANGELOG.md [skip ci] ([`0d7ec3e`](https://github.com/dstroy0/ProtoCore/commit/0d7ec3efb968a8fc8b13d0163293e4a5ae18e169))
- update test report + coverage [skip ci] ([`4f3f7c0`](https://github.com/dstroy0/ProtoCore/commit/4f3f7c0abca1f6e120965297912d4ff1fb4c8b2d))
- update CHANGELOG.md [skip ci] ([`2febb1c`](https://github.com/dstroy0/ProtoCore/commit/2febb1cf05f89270358aa51072a3153441a652bf))
- update CHANGELOG.md [skip ci] ([`0130b4c`](https://github.com/dstroy0/ProtoCore/commit/0130b4c09e9fdddd91ff1063a07d0c1cfd2e617b))
- update CHANGELOG.md [skip ci] ([`97d6381`](https://github.com/dstroy0/ProtoCore/commit/97d6381beba48fff2fb04d524b2858c789286e39))

### Changes

- Bump version: 1.0.0 → 1.0.1 ([`cb08808`](https://github.com/dstroy0/ProtoCore/commit/cb08808a5592e173de340869b31a1389a7076c6a))
- target build fixes ([`648b862`](https://github.com/dstroy0/ProtoCore/commit/648b86252ed200cd2f60be95ae991a3441663dd4))
- Merge branch 'main' of https://github.com/dstroy0/ProtoCore ([`b2f6458`](https://github.com/dstroy0/ProtoCore/commit/b2f6458969da5b8e64398737a0a2bcd7a055cb8b))

### Features

- an NVS seam in board_drivers; the core stops naming Preferences, String and FreeRTOS ([`136c8df`](https://github.com/dstroy0/ProtoCore/commit/136c8df0ddc9f9cacf59e50a49a068127604d106))

### Refactor

- drop the rationale that only justified a coverage exclusion ([`a05bf55`](https://github.com/dstroy0/ProtoCore/commit/a05bf55cd30a951ece558935ccd47259fce565df))
- remove every gcovr exclusion marker from src/ ([`bfd0d5f`](https://github.com/dstroy0/ProtoCore/commit/bfd0d5f996613ade4a85970ff4e99bade584f8e8))
- the event record leaves tcp.h, so the sketches stop parsing the slots ([`a798027`](https://github.com/dstroy0/ProtoCore/commit/a79802731a72f7233ace094d60e492dc44c4d1b0))
- move stdatomic.h from types.h to ring.h ([`26ea42b`](https://github.com/dstroy0/ProtoCore/commit/26ea42b8af0977d310d393d7bb8af3595766c9c4))

### Testing

- the config-store env builds the host NVS backend it now sits on ([`d553ce1`](https://github.com/dstroy0/ProtoCore/commit/d553ce1ecb39a217c0baeafc054fe73ca2eae884))

</details>

## [1.0.0] - 2026-08-04

<details>
<summary><b>Show Changelog for version 1.0.0 - 2026-08-04</b></summary>

### Bug Fixes

- the GPIO direction enum a board profile's macro was rewriting ([`6e9d561`](https://github.com/dstroy0/ProtoCore/commit/6e9d5613337c636dc6414aa52fc191107ced9bae))
- the RAII scope guards and member initializers left in .c files ([`6652612`](https://github.com/dstroy0/ProtoCore/commit/6652612dfb2c0cbbffad2a85c2fa0aa704fbc025))
- the C++ residue the .c rename hid, and the API drift under it ([`49543ac`](https://github.com/dstroy0/ProtoCore/commit/49543ac019853c0f783be73f268531654576776e))
- -std=c11 hid strnlen from every native build ([`262ab91`](https://github.com/dstroy0/ProtoCore/commit/262ab9117328b0d44e8bbfbccfe55d04df65aefe))
- dma.c's byte_ring becomes free functions over a pointer ([`1306696`](https://github.com/dstroy0/ProtoCore/commit/1306696793cebae80b39df6d28ddd2259b2bd6bc))
- static inline in the C headers ([`52c571e`](https://github.com/dstroy0/ProtoCore/commit/52c571ed8dcf918bc88895cb51807c0a6e8e94de))
- hand the listen pcb back to the stack on listener_stop ([`b70bb7e`](https://github.com/dstroy0/ProtoCore/commit/b70bb7eb79f1881eccd816baec890d2cce10d045))
- restore the SNMP_TAG_ prefix, WAL pointer params, and the BerEnc forward typedef ([`88efecc`](https://github.com/dstroy0/ProtoCore/commit/88efecc73952d060f17b5378a5058094bb6b0557))
- refuse to remove a mount root, at the layer that knows it is one ([`a1d50e6`](https://github.com/dstroy0/ProtoCore/commit/a1d50e62f6c72f058df89a8b1b87e22f84776025))
- search the Allow buffer to its NUL, not to its capacity ([`3a93744`](https://github.com/dstroy0/ProtoCore/commit/3a9374409315b928f31ef27fe29b4e3e12d54e70))
- do not drive the fixture volume to the block littlefs cannot recover from ([`ca03735`](https://github.com/dstroy0/ProtoCore/commit/ca037352fcf8327e80bfedf88bc71ebcc8a99b59))
- close open files before unmounting the fixture volume ([`52c5a07`](https://github.com/dstroy0/ProtoCore/commit/52c5a0725c7ca18294b688b26462150022114184))
- a reserved handle must never reach littlefs ([`3b64a8a`](https://github.com/dstroy0/ProtoCore/commit/3b64a8a4c618ffa19ced9deed14dbc12131275b3))
- spell swar's width assert so C++ can parse it, and typedef MockHdr ([`e8e3296`](https://github.com/dstroy0/ProtoCore/commit/e8e3296cc75340db75ab44e07c96da2fddb99ab1))
- stop 404-ing a static mount that named no backend ([`e407e63`](https://github.com/dstroy0/ProtoCore/commit/e407e633983afec0fab97e61cdceee25bb8b047d))
- stop two envs overriding the src filter they inherit ([`94db75d`](https://github.com/dstroy0/ProtoCore/commit/94db75dbb934fe10ee54ae2ef0a661c8815d279c))
- reset the middleware chain with the rest of the server ([`48eccdd`](https://github.com/dstroy0/ProtoCore/commit/48eccdd4685ff5e3fe4103f564d0429f6a6adb9b))
- give the host driver's state one instance instead of one per TU ([`43f9c41`](https://github.com/dstroy0/ProtoCore/commit/43f9c416bf9f25e13569f108e5b702d1fa1bfad1))
- drop the leftovers of the query redesign ([`d5698d2`](https://github.com/dstroy0/ProtoCore/commit/d5698d276df47227973d028250647c4cd0432363))
- restore the v0.0.1 query and path-parameter behavior ([`6050ce4`](https://github.com/dstroy0/ProtoCore/commit/6050ce4793465af0bac7d5dc5b629922f7e97287))
- route the session drain through the platform queue seam ([`a8fa333`](https://github.com/dstroy0/ProtoCore/commit/a8fa33372b8e9ece9b02fb0443f2ac5b23e58bfa))
- repoint the checker baselines the C conversion orphaned ([`1476252`](https://github.com/dstroy0/ProtoCore/commit/14762523da710113549a6aaf8165ea244feb0109))
- name the incomplete struct tag in the SSH GCM wipe casts ([`770fa67`](https://github.com/dstroy0/ProtoCore/commit/770fa67576af3da5d8ae443f5d025567810875ae))
- repoint the Sonar suppressions the C conversion orphaned ([`20d73cb`](https://github.com/dstroy0/ProtoCore/commit/20d73cb21e11a984d417aa39f25dad0772be44e6))
- link the clock TU into every native env that reads it ([`183e693`](https://github.com/dstroy0/ProtoCore/commit/183e693528cb219327c7a123679eadce290c8422))
- convert four more owned-context structs out of C++ ([`586cfd4`](https://github.com/dstroy0/ProtoCore/commit/586cfd4603660e8073792292404649932949a03e))
- strip 772 verified scope qualifiers from test/ and examples/ ([`eb59261`](https://github.com/dstroy0/ProtoCore/commit/eb59261d7287fa8064e5b8cb1c1ccec5cf9005f3))
- finish the C to C++ residue in the CoAP server ([`bb25d76`](https://github.com/dstroy0/ProtoCore/commit/bb25d764c8a89de9278f5029588a42d40a0490f0))
- drop the in-class initializers from OpcuaCtx and hoist proto_handler.h ([`e398dfb`](https://github.com/dstroy0/ProtoCore/commit/e398dfb72c88b3bc9c22966c77e984abcb0ebd72))
- replace the pc_atomic template and in-class initializers in two servers ([`fd1a387`](https://github.com/dstroy0/ProtoCore/commit/fd1a387ae52b3568a70ad041ba17da7a2cc4006b))
- spell the opaque H3Conn tag and hoist a lambda out of test_quic_server ([`794cd33`](https://github.com/dstroy0/ProtoCore/commit/794cd33df4b4be0b431f9f87e88c25b6a715a52e))
- strip 516 verified C++ scope qualifiers from code ([`fb95b57`](https://github.com/dstroy0/ProtoCore/commit/fb95b573540c707da29e25c12dcd6602436284dc))
- spell the opaque QuicConn tag and drop a missed reinterpret_cast ([`03a3609`](https://github.com/dstroy0/ProtoCore/commit/03a3609c9b40ebb1a360718eb03e5b7f53616823))
- strip the QuicTp scope qualifier in quic_tp ([`7428210`](https://github.com/dstroy0/ProtoCore/commit/7428210b298e78ad086884ab087ca618b262fa5a))
- replace the SecureBorrow RAII in quic_crypto with the C pool API ([`960430e`](https://github.com/dstroy0/ProtoCore/commit/960430e2e4f2f091b9f4c4081322c8eb9753eebf))
- spell alignof and the opaque AEAD key the C way in the portable backend ([`506a3d5`](https://github.com/dstroy0/ProtoCore/commit/506a3d5cb4d3b52577ecab53c31b2241792d2593))
- finish the C++ to C conversion in the QUIC/TLS handshake path ([`f657d7e`](https://github.com/dstroy0/ProtoCore/commit/f657d7ec187ef8349a59d838e69ca216017e2649))
- restore constant names mangled by the C++ to C conversion ([`2a639ea`](https://github.com/dstroy0/ProtoCore/commit/2a639ea418d29874bf40582f94504aff567ca15f))
- emit binary_asset_blobs as C and drop the resurrected .cpp ([`3da92d9`](https://github.com/dstroy0/ProtoCore/commit/3da92d94bd7ebb9cc49de3bf6b3a6b7018bc1c96))
- read .c sources in the checkers the C conversion left behind ([`8eb81fa`](https://github.com/dstroy0/ProtoCore/commit/8eb81fa075ac8b17a6313ae053d9c7fd6bb6da62))
- give ip.h C linkage so C++ callers link against it ([`c1ad16a`](https://github.com/dstroy0/ProtoCore/commit/c1ad16aa3b8ff9bea85acb94e65e4029eeadb8e4))
- restore the enum widths the C conversion dropped ([`6bc50b1`](https://github.com/dstroy0/ProtoCore/commit/6bc50b19daaf6754c3b0d98edab1c6d5ed3fed9b))
- make the checkers agree with a C11 tree ([`0c2bd52`](https://github.com/dstroy0/ProtoCore/commit/0c2bd52a962b3a93b6ce865868debb3cb3089aaa))

### CI / Build

- update test report + coverage [skip ci] ([`53bbbef`](https://github.com/dstroy0/ProtoCore/commit/53bbbef1a83a63e61ac64eadf58fd24f78dbb198))
- update CHANGELOG.md [skip ci] ([`82527ff`](https://github.com/dstroy0/ProtoCore/commit/82527ff19dee1e35bd3ce52fc8761d9e3726f57a))
- update CHANGELOG.md [skip ci] ([`dd4a2b9`](https://github.com/dstroy0/ProtoCore/commit/dd4a2b94d1f700890bea3f76abac8fc6f817dc77))
- update CHANGELOG.md [skip ci] ([`45ceb02`](https://github.com/dstroy0/ProtoCore/commit/45ceb02b1803f685e50d24d7de0f707b1ab66860))
- update CHANGELOG.md [skip ci] ([`f22959e`](https://github.com/dstroy0/ProtoCore/commit/f22959e11c424e2a2fa0d7e927fb59ef1cb5ff01))
- update CHANGELOG.md [skip ci] ([`79e0f5e`](https://github.com/dstroy0/ProtoCore/commit/79e0f5e5749cabb985fc0fbd306ffd2df4ecda43))
- update CHANGELOG.md [skip ci] ([`c89b9bc`](https://github.com/dstroy0/ProtoCore/commit/c89b9bcf7d9d9ed86d27d36475a23e48c73f43d6))
- update test report + coverage [skip ci] ([`a80ef6d`](https://github.com/dstroy0/ProtoCore/commit/a80ef6db2a0e97f9256a6c48eb880dd0bb16691f))
- update CHANGELOG.md [skip ci] ([`4002687`](https://github.com/dstroy0/ProtoCore/commit/400268761975bd1f1a29ed627d3ccfe1d8cfc541))
- update CHANGELOG.md [skip ci] ([`e55865a`](https://github.com/dstroy0/ProtoCore/commit/e55865a97e01e6375fd0095cf8c1574a915d97f2))
- update CHANGELOG.md [skip ci] ([`2f86534`](https://github.com/dstroy0/ProtoCore/commit/2f86534c918c0c245fc8788fd1e634b65476e6cb))
- update test report + coverage [skip ci] ([`3b4d006`](https://github.com/dstroy0/ProtoCore/commit/3b4d006c4deb528472de654b74805f67d1e99166))
- update CHANGELOG.md [skip ci] ([`99dafc8`](https://github.com/dstroy0/ProtoCore/commit/99dafc82633e1f9be44c1d1f63de39875059a4dd))
- update test report + coverage [skip ci] ([`2766b6f`](https://github.com/dstroy0/ProtoCore/commit/2766b6f2757f09e05493fe27b2d52c41a8e64447))
- update CHANGELOG.md [skip ci] ([`ada0f6e`](https://github.com/dstroy0/ProtoCore/commit/ada0f6e80b9a1cc737d198c13a9ded6d55fc9b6e))
- update CHANGELOG.md [skip ci] ([`e9edadf`](https://github.com/dstroy0/ProtoCore/commit/e9edadf9aaca29318f2e95107ce23bdf4aa77740))
- update CHANGELOG.md [skip ci] ([`9ef71bb`](https://github.com/dstroy0/ProtoCore/commit/9ef71bb4d467804d361fc40502fcf43ffa45178d))
- update CHANGELOG.md [skip ci] ([`899bd9c`](https://github.com/dstroy0/ProtoCore/commit/899bd9ca85a2055f99c32498fe33efbfbdcbf6d1))
- update CHANGELOG.md [skip ci] ([`2290f20`](https://github.com/dstroy0/ProtoCore/commit/2290f20fc69ad7ef44ccb61d004e88681880f6c3))
- update CHANGELOG.md [skip ci] ([`7d69553`](https://github.com/dstroy0/ProtoCore/commit/7d69553b1cb9e620868b433a8f0b46fce5cdd5c8))
- update CHANGELOG.md [skip ci] ([`1070f26`](https://github.com/dstroy0/ProtoCore/commit/1070f267c51b202f0dd3ac8a50f08cadb77fb24e))
- update CHANGELOG.md [skip ci] ([`54351fc`](https://github.com/dstroy0/ProtoCore/commit/54351fc3bef8ba9c5fa7ba1616099ae94efc9a68))
- update CHANGELOG.md [skip ci] ([`6023235`](https://github.com/dstroy0/ProtoCore/commit/6023235a99d94e406a52f8192f452df2efb1e126))
- update CHANGELOG.md [skip ci] ([`b31e5d1`](https://github.com/dstroy0/ProtoCore/commit/b31e5d1d208d74b741cef460a1d5f94a9e0aa446))
- update CHANGELOG.md [skip ci] ([`7655c0f`](https://github.com/dstroy0/ProtoCore/commit/7655c0f9094b498f5861e50d3bbec186bff3751d))
- update CHANGELOG.md [skip ci] ([`9919310`](https://github.com/dstroy0/ProtoCore/commit/99193102fbfe83b29a97f8cb9e1d2a5b15842b0e))
- update CHANGELOG.md [skip ci] ([`154f7e9`](https://github.com/dstroy0/ProtoCore/commit/154f7e9141faa3da71d7291c7178eda3a07db0d3))
- update CHANGELOG.md [skip ci] ([`94177b3`](https://github.com/dstroy0/ProtoCore/commit/94177b3bbc8e3f61111853a2e290fb1d0b40f62d))
- update CHANGELOG.md [skip ci] ([`b4a7a48`](https://github.com/dstroy0/ProtoCore/commit/b4a7a48a7ade52c519b421c81e74f0c71f54c3aa))
- update CHANGELOG.md [skip ci] ([`7829ec0`](https://github.com/dstroy0/ProtoCore/commit/7829ec061c271ceb6c09e5320d08c251add43e91))
- update test report + coverage [skip ci] ([`bff06c0`](https://github.com/dstroy0/ProtoCore/commit/bff06c01f2e8d17a036d26f632f16a1f4505ef69))
- update CHANGELOG.md [skip ci] ([`700e016`](https://github.com/dstroy0/ProtoCore/commit/700e016c643bd0aa833bdcc3d0104f5f8f707687))
- update CHANGELOG.md [skip ci] ([`f97963b`](https://github.com/dstroy0/ProtoCore/commit/f97963bb555e7ef6d1c74f7902fa5513407a5e98))
- update CHANGELOG.md [skip ci] ([`b6f7596`](https://github.com/dstroy0/ProtoCore/commit/b6f7596c30912a4b85ed520e2d98881c63b2152f))
- update CHANGELOG.md [skip ci] ([`b81a73a`](https://github.com/dstroy0/ProtoCore/commit/b81a73a07cd5527508c8a893a3d63e459978b827))
- update CHANGELOG.md [skip ci] ([`647b62c`](https://github.com/dstroy0/ProtoCore/commit/647b62c0c019b9f05982b865529b67dcef728b09))
- update CHANGELOG.md [skip ci] ([`55a1149`](https://github.com/dstroy0/ProtoCore/commit/55a1149162734ad632f778781fcf499c041aef01))
- update CHANGELOG.md [skip ci] ([`66db608`](https://github.com/dstroy0/ProtoCore/commit/66db608a0c80cc820346ded35f6aa6f62cfd3794))
- update CHANGELOG.md [skip ci] ([`d5fd6f5`](https://github.com/dstroy0/ProtoCore/commit/d5fd6f5c5892ce8dab75398807b5820f7d99da32))
- update CHANGELOG.md [skip ci] ([`e98383f`](https://github.com/dstroy0/ProtoCore/commit/e98383f6f20f7abfef52870e63050a54851b45eb))
- update CHANGELOG.md [skip ci] ([`3c773d6`](https://github.com/dstroy0/ProtoCore/commit/3c773d6852bb951f6ca5707225868ddbabb0af27))
- update test report + coverage [skip ci] ([`ca2dfc7`](https://github.com/dstroy0/ProtoCore/commit/ca2dfc722ad6ed164289757c0bbfc2d1d9bc7c0f))
- update CHANGELOG.md [skip ci] ([`063cd9c`](https://github.com/dstroy0/ProtoCore/commit/063cd9cb98ef9df9e3996457090642f28d2810d0))
- update CHANGELOG.md [skip ci] ([`e396f68`](https://github.com/dstroy0/ProtoCore/commit/e396f68dd0eb3f737587257cbf33754f4f700146))
- update CHANGELOG.md [skip ci] ([`9592949`](https://github.com/dstroy0/ProtoCore/commit/95929499fad7942466f3746074e6204f2b2c9923))
- update CHANGELOG.md [skip ci] ([`eab059e`](https://github.com/dstroy0/ProtoCore/commit/eab059e23e8ff97b50c50956504a48237855682a))
- update CHANGELOG.md [skip ci] ([`c7c9078`](https://github.com/dstroy0/ProtoCore/commit/c7c90789b343c8a3d107628c93a16022eb221768))
- update CHANGELOG.md [skip ci] ([`a042d94`](https://github.com/dstroy0/ProtoCore/commit/a042d9470615b7ed0f905e2955862738a76d9549))
- update CHANGELOG.md [skip ci] ([`e26b53f`](https://github.com/dstroy0/ProtoCore/commit/e26b53f7824a4f5417547140febc5673319f05d7))
- update CHANGELOG.md [skip ci] ([`ef845a4`](https://github.com/dstroy0/ProtoCore/commit/ef845a4b3f4c3279afdfae78bd21d193016267b5))
- update CHANGELOG.md [skip ci] ([`07e151d`](https://github.com/dstroy0/ProtoCore/commit/07e151d54a63e2894a9ba8e56959430344364611))
- update CHANGELOG.md [skip ci] ([`2877f9f`](https://github.com/dstroy0/ProtoCore/commit/2877f9f911990d199c86e9a4a04e9ce11d8a1fc7))
- update CHANGELOG.md [skip ci] ([`a74dfd9`](https://github.com/dstroy0/ProtoCore/commit/a74dfd93ab4a546d29955ab8f9352b5f2ac407d4))
- update CHANGELOG.md [skip ci] ([`b468456`](https://github.com/dstroy0/ProtoCore/commit/b468456fb315a99958491667752fba85b1d2efb8))
- update CHANGELOG.md [skip ci] ([`09e6682`](https://github.com/dstroy0/ProtoCore/commit/09e6682cd48080970ecde9b23abcf2412c4df955))
- update CHANGELOG.md [skip ci] ([`dcda99b`](https://github.com/dstroy0/ProtoCore/commit/dcda99b00da72981ce97a0139acfb26c36d58179))
- update CHANGELOG.md [skip ci] ([`ccc6e7e`](https://github.com/dstroy0/ProtoCore/commit/ccc6e7e7bf5a15c391195004b95a51da22c3bffd))
- update CHANGELOG.md [skip ci] ([`149bb8a`](https://github.com/dstroy0/ProtoCore/commit/149bb8a5e7bf4175209a6f9dc179b4089b75592c))
- update CHANGELOG.md [skip ci] ([`047b662`](https://github.com/dstroy0/ProtoCore/commit/047b662beb5b491e0eec06bd7772c6c69627bc5e))
- update CHANGELOG.md [skip ci] ([`e5ab957`](https://github.com/dstroy0/ProtoCore/commit/e5ab9575127bf666817180f16ea62aac3402791c))
- update CHANGELOG.md [skip ci] ([`e63cf94`](https://github.com/dstroy0/ProtoCore/commit/e63cf9411155e0c18dbaa14d2d69d27a35616ea2))
- update CHANGELOG.md [skip ci] ([`8a830a0`](https://github.com/dstroy0/ProtoCore/commit/8a830a0c0f8c29b910a870cdcc8054a9836e765c))
- update CHANGELOG.md [skip ci] ([`6176780`](https://github.com/dstroy0/ProtoCore/commit/617678047959afcb731f8c4c4c17500fb907b18a))
- update CHANGELOG.md [skip ci] ([`980290e`](https://github.com/dstroy0/ProtoCore/commit/980290e3c7d81b0559eeee210aa574499fd09ed8))
- update CHANGELOG.md [skip ci] ([`1e32f1a`](https://github.com/dstroy0/ProtoCore/commit/1e32f1a5e552da840c92c61a0b25cf2022abfd2f))
- update CHANGELOG.md [skip ci] ([`1ee9964`](https://github.com/dstroy0/ProtoCore/commit/1ee9964a95030b3fc98b4d68973d29011ad8cb2d))
- update CHANGELOG.md [skip ci] ([`23df5d4`](https://github.com/dstroy0/ProtoCore/commit/23df5d49398bacf3bf441730c7c6d33b7c935596))
- update CHANGELOG.md [skip ci] ([`74c02f8`](https://github.com/dstroy0/ProtoCore/commit/74c02f8a7624aec4283bb95c5d7ea2cbd7b9a6f4))
- update CHANGELOG.md [skip ci] ([`cbdb3fc`](https://github.com/dstroy0/ProtoCore/commit/cbdb3fc9e1c636ec8da56eec461342428ab6662f))
- update CHANGELOG.md [skip ci] ([`fba71e5`](https://github.com/dstroy0/ProtoCore/commit/fba71e59277d1e615d335390138d3874e03048b0))
- update CHANGELOG.md [skip ci] ([`dd0cf87`](https://github.com/dstroy0/ProtoCore/commit/dd0cf87d1ecdd9a941f0a5f6430f5716a05ed212))
- update CHANGELOG.md [skip ci] ([`fc6720f`](https://github.com/dstroy0/ProtoCore/commit/fc6720f5570454879c2e16b5671ab2c88eb38686))
- update CHANGELOG.md [skip ci] ([`53d3768`](https://github.com/dstroy0/ProtoCore/commit/53d3768544ceecbe9cea6597f840f02fa0861aa1))
- update CHANGELOG.md [skip ci] ([`372545b`](https://github.com/dstroy0/ProtoCore/commit/372545b1d4affba2555996badad047131047b9ad))
- update CHANGELOG.md [skip ci] ([`3fa1036`](https://github.com/dstroy0/ProtoCore/commit/3fa103690c8ddc18b88bfddf0f48f9efe2b512d9))
- update CHANGELOG.md [skip ci] ([`fecc38b`](https://github.com/dstroy0/ProtoCore/commit/fecc38b27a01eb43ecd305afb49d5abbb7a65914))
- update CHANGELOG.md [skip ci] ([`fb6eb2f`](https://github.com/dstroy0/ProtoCore/commit/fb6eb2f46c31f0d734411d5e9ca04a8a40c9a15d))
- update CHANGELOG.md [skip ci] ([`e74c164`](https://github.com/dstroy0/ProtoCore/commit/e74c1642e03de357df71b60ee1147cb9b93df906))
- update test report + coverage [skip ci] ([`862b804`](https://github.com/dstroy0/ProtoCore/commit/862b8043f655280494239b2609372ea0b30f1332))
- update CHANGELOG.md [skip ci] ([`4d01c56`](https://github.com/dstroy0/ProtoCore/commit/4d01c56b26b26c7d945456ea24d92fe18aecf70e))
- update CHANGELOG.md [skip ci] ([`1e816ab`](https://github.com/dstroy0/ProtoCore/commit/1e816abe968cc2736e7a22be58718880f8a06f5c))
- update CHANGELOG.md [skip ci] ([`e21315c`](https://github.com/dstroy0/ProtoCore/commit/e21315cba6aa77e7bd6459eb189c8c5bd0479973))
- update CHANGELOG.md [skip ci] ([`5607ab2`](https://github.com/dstroy0/ProtoCore/commit/5607ab2667c17f6a7fd6bcfcedd5c80efc31981c))
- update CHANGELOG.md [skip ci] ([`b9a717a`](https://github.com/dstroy0/ProtoCore/commit/b9a717a7e1d274b743601be4c47a20ecbd76370f))
- update CHANGELOG.md [skip ci] ([`52fc242`](https://github.com/dstroy0/ProtoCore/commit/52fc242c7e62a7f2574e6c0b3320fce155885591))
- update CHANGELOG.md [skip ci] ([`9e67986`](https://github.com/dstroy0/ProtoCore/commit/9e6798621951a5cfaae9c85dfd1f32ba91e70438))
- update CHANGELOG.md [skip ci] ([`ae17dab`](https://github.com/dstroy0/ProtoCore/commit/ae17dab8c3b6ad50533a5a299fe1b836f88fda8a))
- update CHANGELOG.md [skip ci] ([`e2e58ee`](https://github.com/dstroy0/ProtoCore/commit/e2e58ee0d130ddb85d190e4b712f3b96640ec132))
- update CHANGELOG.md [skip ci] ([`3997fb2`](https://github.com/dstroy0/ProtoCore/commit/3997fb2e1b181c9f3591a77de9fdda25cc95d97c))
- update CHANGELOG.md [skip ci] ([`9c92256`](https://github.com/dstroy0/ProtoCore/commit/9c92256c1d0a7a91dad5b81c42dfc862cce28676))
- update CHANGELOG.md [skip ci] ([`b4be567`](https://github.com/dstroy0/ProtoCore/commit/b4be5675cd19c39291a84b593193d222d17ea9f8))
- update CHANGELOG.md [skip ci] ([`0741e6b`](https://github.com/dstroy0/ProtoCore/commit/0741e6b7a6af13e8831b53dbbb4724cd3654cd44))
- update CHANGELOG.md [skip ci] ([`b95c654`](https://github.com/dstroy0/ProtoCore/commit/b95c654948a1f75db7f9aad6da68dc095265af9f))
- update CHANGELOG.md [skip ci] ([`af162fc`](https://github.com/dstroy0/ProtoCore/commit/af162fc7f91918185a4c324585d3ac3285b1ceea))
- update CHANGELOG.md [skip ci] ([`f406586`](https://github.com/dstroy0/ProtoCore/commit/f406586e834ad4f2a4fee71e57616eafbef4d4a8))
- update CHANGELOG.md [skip ci] ([`a2b3031`](https://github.com/dstroy0/ProtoCore/commit/a2b303145ae39abfd34d7f9ebd4d349f7be681b6))
- update CHANGELOG.md [skip ci] ([`1789944`](https://github.com/dstroy0/ProtoCore/commit/1789944f224888985c0a8eba87598c177f5f1c80))
- update CHANGELOG.md [skip ci] ([`aee14c8`](https://github.com/dstroy0/ProtoCore/commit/aee14c85cb409a1e744e5548eb941483ab158468))
- update CHANGELOG.md [skip ci] ([`e7b2366`](https://github.com/dstroy0/ProtoCore/commit/e7b2366b12815f420c9ad2569c2c21da54fb86cd))
- generate the native base env, at C11 ([`a44ce4d`](https://github.com/dstroy0/ProtoCore/commit/a44ce4d446871c38eb6efd5942bc767abf3c7c58))
- update CHANGELOG.md [skip ci] ([`1dfbdea`](https://github.com/dstroy0/ProtoCore/commit/1dfbdea7ec7d7463da5b637d27188b1f2a04a814))
- update test report + coverage [skip ci] ([`7ebeb27`](https://github.com/dstroy0/ProtoCore/commit/7ebeb274dc5aa64cf6ff49d0c9646388a3587b31))
- update CHANGELOG.md [skip ci] ([`caf1e1e`](https://github.com/dstroy0/ProtoCore/commit/caf1e1eee99768cc9373281bedc9f452c229a260))
- update test report + coverage [skip ci] ([`68c8c92`](https://github.com/dstroy0/ProtoCore/commit/68c8c9284e2b587a2e19ca35b683523238db23cd))
- update CHANGELOG.md [skip ci] ([`4b3225b`](https://github.com/dstroy0/ProtoCore/commit/4b3225b6229ba2a89e3b3b6aef09f72acb8c9168))
- update CHANGELOG.md [skip ci] ([`729f02f`](https://github.com/dstroy0/ProtoCore/commit/729f02f51254de8ee053ca53c8e34c45fa790bfa))
- bump github/codeql-action from 4 to 4.37.4 ([`5098e9b`](https://github.com/dstroy0/ProtoCore/commit/5098e9b02554f105b77a1fb574ca19f84a54bc96))
- update CHANGELOG.md [skip ci] ([`30ee2b8`](https://github.com/dstroy0/ProtoCore/commit/30ee2b8c0568fe5b26278665e989866c2d756c81))
- update CHANGELOG.md [skip ci] ([`1c2ad03`](https://github.com/dstroy0/ProtoCore/commit/1c2ad03e5ddc5e89a2e6de07c5d009ec0cdd0b65))
- update CHANGELOG.md [skip ci] ([`2974b57`](https://github.com/dstroy0/ProtoCore/commit/2974b57a3dc8a9c4f680fb762c3851c25d8adf3d))
- update CHANGELOG.md [skip ci] ([`ddbe826`](https://github.com/dstroy0/ProtoCore/commit/ddbe826b5788df15315993ee1e9f299ee22ea810))
- update CHANGELOG.md [skip ci] ([`4341255`](https://github.com/dstroy0/ProtoCore/commit/4341255a69d3a34c3bb7d14ad55e03ab50e7ff41))
- update CHANGELOG.md [skip ci] ([`c1f81dc`](https://github.com/dstroy0/ProtoCore/commit/c1f81dc4949462a66b8ebe05795c1248ffb94d02))
- update CHANGELOG.md [skip ci] ([`b14719d`](https://github.com/dstroy0/ProtoCore/commit/b14719d1aa2e66f326991b0e0b26c86f5618237d))
- update CHANGELOG.md [skip ci] ([`f136ce3`](https://github.com/dstroy0/ProtoCore/commit/f136ce35bca290840f132ea93a2777a5fa0c7018))
- update CHANGELOG.md [skip ci] ([`d55e811`](https://github.com/dstroy0/ProtoCore/commit/d55e811f43a34ecf97cfc0a0f369b048aaff7b82))
- update CHANGELOG.md [skip ci] ([`004ba2e`](https://github.com/dstroy0/ProtoCore/commit/004ba2e9b07b24a7ae56b9259dcc8d19a4611a89))
- update CHANGELOG.md [skip ci] ([`b6b5f08`](https://github.com/dstroy0/ProtoCore/commit/b6b5f08a230c8520e27783d1174a27626d8b5964))
- gate on every src/ TU having a test env ([`0a7b6a6`](https://github.com/dstroy0/ProtoCore/commit/0a7b6a69e7169aaa4764e3b43a7e6d81d5f8615c))
- update CHANGELOG.md [skip ci] ([`2fe512c`](https://github.com/dstroy0/ProtoCore/commit/2fe512c240a8270ec2993d7306029fe882f1897d))
- update CHANGELOG.md [skip ci] ([`6abb05f`](https://github.com/dstroy0/ProtoCore/commit/6abb05f330c76574fdb3e5479d1abf794798d5c7))
- update CHANGELOG.md [skip ci] ([`9b73d9b`](https://github.com/dstroy0/ProtoCore/commit/9b73d9bd08ff40beb876c3c3e4bab52d7c75f426))
- update CHANGELOG.md [skip ci] ([`7de0efa`](https://github.com/dstroy0/ProtoCore/commit/7de0efa10d4e0866e726d9f51e28f736ad838970))
- update CHANGELOG.md [skip ci] ([`856d2f2`](https://github.com/dstroy0/ProtoCore/commit/856d2f291073c3036fc54b50c35e6504c259302c))
- update CHANGELOG.md [skip ci] ([`87ca7bd`](https://github.com/dstroy0/ProtoCore/commit/87ca7bd4ce21ecc55f38afab86179c8af6cec1a7))
- update test report + coverage [skip ci] ([`60644ab`](https://github.com/dstroy0/ProtoCore/commit/60644abfe8e373451abceb72d24d0d00113b623b))
- update CHANGELOG.md [skip ci] ([`1cc1ae7`](https://github.com/dstroy0/ProtoCore/commit/1cc1ae739bd82d4a9cd4ff42eee50c8af5c12e3e))
- update CHANGELOG.md [skip ci] ([`de620ec`](https://github.com/dstroy0/ProtoCore/commit/de620ece45ec719c4248e4a16627b0c3391a1f2a))
- update test report + coverage [skip ci] ([`be9d0b7`](https://github.com/dstroy0/ProtoCore/commit/be9d0b792f7771544e6d3ea2024f3e1035576ecf))
- update CHANGELOG.md [skip ci] ([`09bcda6`](https://github.com/dstroy0/ProtoCore/commit/09bcda6c7e6d16409bedddd6f9dd37e76d26ceb2))
- update test report + coverage [skip ci] ([`2e774fb`](https://github.com/dstroy0/ProtoCore/commit/2e774fb9ae6f96aebc2e4b71dbf1b06e5df34daf))
- update CHANGELOG.md [skip ci] ([`39d6728`](https://github.com/dstroy0/ProtoCore/commit/39d67280b18c3dc15add4133c107916ea71d50b2))
- update CHANGELOG.md [skip ci] ([`6495f67`](https://github.com/dstroy0/ProtoCore/commit/6495f67297dd8c1c2c5639229caee0540e807f15))
- update CHANGELOG.md [skip ci] ([`8a33977`](https://github.com/dstroy0/ProtoCore/commit/8a339771ac15e9b1581bd168f2a9bc610cc855ed))
- update test report + coverage [skip ci] ([`adaa1bc`](https://github.com/dstroy0/ProtoCore/commit/adaa1bc70a8af0cdd4380daa7a6334db251ca4c3))
- update CHANGELOG.md [skip ci] ([`14991c1`](https://github.com/dstroy0/ProtoCore/commit/14991c11e16693d97b8e903c71605cdeb0f1b842))
- update CHANGELOG.md [skip ci] ([`ec41e69`](https://github.com/dstroy0/ProtoCore/commit/ec41e69eef350640effdc95b4ae3ba348b5d12f9))
- update test report + coverage [skip ci] ([`89de4e0`](https://github.com/dstroy0/ProtoCore/commit/89de4e0921db8c7a5198ba87c03338c52785140f))
- update CHANGELOG.md [skip ci] ([`6584c65`](https://github.com/dstroy0/ProtoCore/commit/6584c65bcb36f3d674b93ad6b514ed2cd8b801b7))
- update test report + coverage [skip ci] ([`19b5080`](https://github.com/dstroy0/ProtoCore/commit/19b50803aa9b8f081199d11f62af819d3379701a))
- update CHANGELOG.md [skip ci] ([`3227657`](https://github.com/dstroy0/ProtoCore/commit/3227657a08fc51bba13cc90b5005c4da5ddf2bea))
- update test report + coverage [skip ci] ([`3ca3f75`](https://github.com/dstroy0/ProtoCore/commit/3ca3f75b4b8ea37d81879cb458fa05bed4639bdd))
- update CHANGELOG.md [skip ci] ([`a1af822`](https://github.com/dstroy0/ProtoCore/commit/a1af822e0fde44ba653b3f091f83040c5ebc6b0e))
- update test report + coverage [skip ci] ([`b2b1226`](https://github.com/dstroy0/ProtoCore/commit/b2b1226a2919349f3260a7b4f39c65245ff16021))
- update CHANGELOG.md [skip ci] ([`a7621df`](https://github.com/dstroy0/ProtoCore/commit/a7621df589dd738b977d8ec01716f8357b85ab6c))
- update test report + coverage [skip ci] ([`485fd95`](https://github.com/dstroy0/ProtoCore/commit/485fd952e211d2768aab4a4055e5cff938b441ec))
- update CHANGELOG.md [skip ci] ([`ffc765c`](https://github.com/dstroy0/ProtoCore/commit/ffc765c112a12f24080b74068cb113598343fc3f))
- update test report + coverage [skip ci] ([`22587e5`](https://github.com/dstroy0/ProtoCore/commit/22587e5b95d2dfa5c71149070b5fac24499dd573))
- update CHANGELOG.md [skip ci] ([`e41f977`](https://github.com/dstroy0/ProtoCore/commit/e41f97757fc2d294616cd743ddbba46d860f95c5))
- update CHANGELOG.md [skip ci] ([`f1f7376`](https://github.com/dstroy0/ProtoCore/commit/f1f73762f82c0339c729045bbe94bdc7e4676d77))
- update test report + coverage [skip ci] ([`e336f4f`](https://github.com/dstroy0/ProtoCore/commit/e336f4fdeec3519ae8991d804485faade037e787))
- update CHANGELOG.md [skip ci] ([`2970762`](https://github.com/dstroy0/ProtoCore/commit/29707623c374871dcc552747561396a40c1ec2d6))
- update test report + coverage [skip ci] ([`450b3e8`](https://github.com/dstroy0/ProtoCore/commit/450b3e8b3472ccb8c8612e5937f359e674d26d12))
- update CHANGELOG.md [skip ci] ([`9f572b4`](https://github.com/dstroy0/ProtoCore/commit/9f572b4c051a2aa022973bd1ea5384ad69f5cbfd))
- update CHANGELOG.md [skip ci] ([`e5c6df1`](https://github.com/dstroy0/ProtoCore/commit/e5c6df10377b88c0d7d353231343c48d4242f01e))
- update test report + coverage [skip ci] ([`1cd9cdc`](https://github.com/dstroy0/ProtoCore/commit/1cd9cdcd713aaadf302f67de063e948c7b445dc6))
- update CHANGELOG.md [skip ci] ([`2b9c20b`](https://github.com/dstroy0/ProtoCore/commit/2b9c20baa5c87848cd707c2c7190ece7c809c01b))
- update CHANGELOG.md [skip ci] ([`7870cdb`](https://github.com/dstroy0/ProtoCore/commit/7870cdbbfd3fd83f3cf862f3717d36f5929f92f8))
- update test report + coverage [skip ci] ([`f2509a3`](https://github.com/dstroy0/ProtoCore/commit/f2509a3f3cbfc5ede3d792fc378a7875e6f46e9c))
- update CHANGELOG.md [skip ci] ([`40d7ddb`](https://github.com/dstroy0/ProtoCore/commit/40d7ddba723a62d81bc56b7a8c7fa650b528442d))
- update CHANGELOG.md [skip ci] ([`b58bfe4`](https://github.com/dstroy0/ProtoCore/commit/b58bfe4df7b2cd2291c1b782039630f02d5b46d5))
- update test report + coverage [skip ci] ([`8e89fce`](https://github.com/dstroy0/ProtoCore/commit/8e89fcec6f81a900c0526a937b4745a61d33f7e8))
- update CHANGELOG.md [skip ci] ([`6f0da5b`](https://github.com/dstroy0/ProtoCore/commit/6f0da5b8f00d09df64503e03ba9947b61c92f490))
- update CHANGELOG.md [skip ci] ([`d98e86d`](https://github.com/dstroy0/ProtoCore/commit/d98e86d14f1dfc9f22eb383bc5f3e42e8fa36ab8))
- update CHANGELOG.md [skip ci] ([`c683801`](https://github.com/dstroy0/ProtoCore/commit/c68380103cde6f57592d0e9ecc87fd4a5af0729c))
- update test report + coverage [skip ci] ([`4214270`](https://github.com/dstroy0/ProtoCore/commit/4214270b317aa4c4712cec42b4859eb42eeee56e))
- update CHANGELOG.md [skip ci] ([`3e4a9e4`](https://github.com/dstroy0/ProtoCore/commit/3e4a9e4971832eeb481b3b950854b1dc1474c2ce))
- update test report + coverage [skip ci] ([`8d82ee3`](https://github.com/dstroy0/ProtoCore/commit/8d82ee3729b1287ff0d64e76942ef32c52b33dc3))
- update CHANGELOG.md [skip ci] ([`a32dece`](https://github.com/dstroy0/ProtoCore/commit/a32dece4fe3ac4ee0e9295269ba21940a4db8670))
- update test report + coverage [skip ci] ([`2e4e0bc`](https://github.com/dstroy0/ProtoCore/commit/2e4e0bc232efe80d4e93e76d4c82fb2f700b4c5f))
- update CHANGELOG.md [skip ci] ([`f42f5a0`](https://github.com/dstroy0/ProtoCore/commit/f42f5a09bc2eba6254bd2b9d098f1834ef334afd))
- update test report + coverage [skip ci] ([`7653e4c`](https://github.com/dstroy0/ProtoCore/commit/7653e4cbb44e4cd65aa48c5ac3dc11e968ac3263))
- update CHANGELOG.md [skip ci] ([`b632f92`](https://github.com/dstroy0/ProtoCore/commit/b632f926416c3aab876bdfcbf01fbdb5ee107877))
- build the ESP-IDF component on GitHub ([`b58882c`](https://github.com/dstroy0/ProtoCore/commit/b58882c22e95496c37067b597f5bdaf7a5d722f6))
- update CHANGELOG.md [skip ci] ([`5b0eb19`](https://github.com/dstroy0/ProtoCore/commit/5b0eb19f834a6a84becc9edba24b4f4ab8c87a40))
- update CHANGELOG.md [skip ci] ([`2bc470d`](https://github.com/dstroy0/ProtoCore/commit/2bc470d7f1f5a95a7e911c7d419a989a1c293501))
- update CHANGELOG.md [skip ci] ([`906bc4a`](https://github.com/dstroy0/ProtoCore/commit/906bc4a0ae923616d1448d33e881531f5286d3a9))
- update CHANGELOG.md [skip ci] ([`9f9e696`](https://github.com/dstroy0/ProtoCore/commit/9f9e6969e066a5f2aeae51c671fa9cae990ea552))
- update CHANGELOG.md [skip ci] ([`92692fe`](https://github.com/dstroy0/ProtoCore/commit/92692feeb69f0fb1eae77e48e94ab7f94f65cf2f))
- update CHANGELOG.md [skip ci] ([`35227fe`](https://github.com/dstroy0/ProtoCore/commit/35227fe81e139d38478bdabfd176d0d415685b18))
- update CHANGELOG.md [skip ci] ([`f8ee54e`](https://github.com/dstroy0/ProtoCore/commit/f8ee54e88c0686db636b3f4e39622c62322fc47e))

### Changes

- Bump version: 0.0.7 → 1.0.0 ([`e886b3c`](https://github.com/dstroy0/ProtoCore/commit/e886b3cb07662abe6ff3d86093b83b76055f8db5))
- Revert "test: copying onto the root collection is refused, not created" ([`7cda282`](https://github.com/dstroy0/ProtoCore/commit/7cda282cc97f95d3fd756943bb9072925938bb11))
- Revert "test: remount after filling, so the fixture starts from the medium" ([`e19b85f`](https://github.com/dstroy0/ProtoCore/commit/e19b85f48e5c12c39baf91ee0e7cfb4a47e3cc58))
- Revert "fix: do not drive the fixture volume to the block littlefs cannot recover from" ([`a348f72`](https://github.com/dstroy0/ProtoCore/commit/a348f724a0d85945fb7378453924b662e47803d3))
- Merge Dependabot #21: build(deps): bump github/codeql-action from 4 to 4.37.4 ([`e587e2a`](https://github.com/dstroy0/ProtoCore/commit/e587e2a935c429d3163a536015960c6e5da06a76))
- clang-format the two benches the layer move left unformatted ([`2db2997`](https://github.com/dstroy0/ProtoCore/commit/2db299722f2a5d330b198a71e4cc7fd18702b47f))

### Documentation

- log the HTTP/2 refusal that RSTs stream 0 ([`58c2913`](https://github.com/dstroy0/ProtoCore/commit/58c29137dc02d6d9d1300bb5b63c232cee717f85))
- regenerate feature tables + configurator + build_opt.h + example index [skip ci] ([`3d8b3c4`](https://github.com/dstroy0/ProtoCore/commit/3d8b3c4bdcae4e29bda2632596e245e4aaeba1e8))
- regenerate feature tables + configurator + build_opt.h + example index [skip ci] ([`aaffcd0`](https://github.com/dstroy0/ProtoCore/commit/aaffcd03396e6da0343f1bb6a42e264df3af5946))
- regenerate feature tables + configurator + build_opt.h + example index [skip ci] ([`b193396`](https://github.com/dstroy0/ProtoCore/commit/b1933961007eaecd77b9b7f2524689ca9900e442))
- regenerate feature tables + configurator + build_opt.h + example index [skip ci] ([`a3760f2`](https://github.com/dstroy0/ProtoCore/commit/a3760f23ae4ea7ca8bc9a445abafbe22d20504ac))
- regenerate feature tables + configurator + build_opt.h + example index [skip ci] ([`0331ff5`](https://github.com/dstroy0/ProtoCore/commit/0331ff5282b4ae427a18a405f6dbe4f3a28a0e30))
- regenerate feature tables + configurator + build_opt.h + example index [skip ci] ([`823cc89`](https://github.com/dstroy0/ProtoCore/commit/823cc898d0f2c1e07223dd4e0280cd83d728c9ae))
- regenerate feature tables + configurator + build_opt.h + example index [skip ci] ([`506e29c`](https://github.com/dstroy0/ProtoCore/commit/506e29cacdb62b548ffd701d15c0d7391ecc18b5))
- regenerate feature tables + configurator + build_opt.h + example index [skip ci] ([`18ca7d1`](https://github.com/dstroy0/ProtoCore/commit/18ca7d1027f2026e0e23dc78f7f5951da299106f))
- regenerate feature tables + configurator + build_opt.h + example index [skip ci] ([`33e79ed`](https://github.com/dstroy0/ProtoCore/commit/33e79ed30353153c100a1ea86fe6a67a8153985c))
- regenerate feature tables + configurator + build_opt.h + example index [skip ci] ([`9a18380`](https://github.com/dstroy0/ProtoCore/commit/9a18380b1f77a4afce3dff8f9a93f96f1d69dca1))
- regenerate feature tables + configurator + build_opt.h + example index [skip ci] ([`97e73b6`](https://github.com/dstroy0/ProtoCore/commit/97e73b690f926278af32bea0a9a87dc3dff9480a))
- regenerate feature tables + configurator + build_opt.h + example index [skip ci] ([`aa3d2f2`](https://github.com/dstroy0/ProtoCore/commit/aa3d2f2d4bbc61051b7d440bc6169d26f0a49d7e))
- correct what the SSH-mount entry claims is already tested ([`95e96ce`](https://github.com/dstroy0/ProtoCore/commit/95e96ce90f1b18830490b67687a79032c8bd1fb7))
- roadmap the SSH mount and the multipoint mnt it needs ([`39a1a99`](https://github.com/dstroy0/ProtoCore/commit/39a1a99a37bf1b1ede7b69f13f5ccac785bd8c96))
- regenerate feature tables + configurator + build_opt.h + example index [skip ci] ([`cc3e721`](https://github.com/dstroy0/ProtoCore/commit/cc3e7213562864d8b3389377526eb0a5333a648c))
- regenerate feature tables + configurator + build_opt.h + example index [skip ci] ([`d1cea96`](https://github.com/dstroy0/ProtoCore/commit/d1cea965f967058099e5aeb8414f06bd734aebc6))
- regenerate feature tables + configurator + build_opt.h + example index [skip ci] ([`e8a0034`](https://github.com/dstroy0/ProtoCore/commit/e8a003460b0a7525892b7178c76658fce6c6399b))
- regenerate feature tables + configurator + build_opt.h + example index [skip ci] ([`0af15b8`](https://github.com/dstroy0/ProtoCore/commit/0af15b881340d16b7db7f1c7f7d45b87559cc6ed))
- regenerate feature tables + configurator + build_opt.h + example index [skip ci] ([`4fc79d3`](https://github.com/dstroy0/ProtoCore/commit/4fc79d3eaed4b8820ec5b3c2b403a8c233c5b2d6))
- regenerate feature tables + configurator + build_opt.h + example index [skip ci] ([`7ca75f0`](https://github.com/dstroy0/ProtoCore/commit/7ca75f028cb39a7f529e2ed9dabaa5f3cf5e33e1))
- repoint the BUGS.md citation of the presentation layer to its .c path ([`c3cdaaa`](https://github.com/dstroy0/ProtoCore/commit/c3cdaaa1b35003446a5b2e8560c9ec519564ce12))
- regenerate feature tables + configurator + build_opt.h + example index [skip ci] ([`ac0ff85`](https://github.com/dstroy0/ProtoCore/commit/ac0ff85df953edf153ee892d811895332691c9be))
- regenerate feature tables + configurator + build_opt.h + example index [skip ci] ([`5656c29`](https://github.com/dstroy0/ProtoCore/commit/5656c295f8cfeff71b00e93ade9447db4dc14364))
- add pass-the-reference-down to the end of the roadmap ([`fe124e7`](https://github.com/dstroy0/ProtoCore/commit/fe124e786f6c7ac407d5c260f8f2198c8b0ac776))
- regenerate feature tables + configurator + build_opt.h + example index [skip ci] ([`f50be82`](https://github.com/dstroy0/ProtoCore/commit/f50be824906686817bbec97bf82bbce289203be6))
- regenerate feature tables + configurator + build_opt.h + example index [skip ci] ([`ec1fbd1`](https://github.com/dstroy0/ProtoCore/commit/ec1fbd1e3051b887a499fa71db1eae8aa30b3354))
- regenerate feature tables + configurator + build_opt.h + example index [skip ci] ([`f48be9f`](https://github.com/dstroy0/ProtoCore/commit/f48be9f74fc73d163374b5de90f882460162be90))
- regenerate feature tables + configurator + build_opt.h + example index [skip ci] ([`c25ce38`](https://github.com/dstroy0/ProtoCore/commit/c25ce38d988f90b311ad1a5d98bb169598a5aa4f))
- document the mmgr memory model ([`a2b6c58`](https://github.com/dstroy0/ProtoCore/commit/a2b6c5828b9a2699cc5d3d030a3c87108fd81f72))
- strip narrative from the i2c and proto_builtins comments ([`8e303b5`](https://github.com/dstroy0/ProtoCore/commit/8e303b5cf49a525a492d16cd687f7116bbf48b17))
- regenerate feature tables + configurator + build_opt.h + example index [skip ci] ([`29e3da9`](https://github.com/dstroy0/ProtoCore/commit/29e3da95aea09a5bcc47a54a4db5f4fb34137035))
- describe diag as the runtime frame build it is ([`2e4176f`](https://github.com/dstroy0/ProtoCore/commit/2e4176f9327d4c8da9eaee07416942a64002a953))
- regenerate feature tables + configurator + build_opt.h + example index [skip ci] ([`5127742`](https://github.com/dstroy0/ProtoCore/commit/51277427f26c15063c7af53ff87bc185c0ebe7cb))
- regenerate feature tables + configurator + build_opt.h + example index [skip ci] ([`7f88800`](https://github.com/dstroy0/ProtoCore/commit/7f88800b01dfd0308e1487019dac72888363b3d7))

### Features

- add a mock vendor so the hot path is testable on a host ([`a53832c`](https://github.com/dstroy0/ProtoCore/commit/a53832c3633703af390f753eec3e1e3a2f35f0a7))

### Refactor

- finish the C11 conversion - the last seven .cpp files ([`eb378e6`](https://github.com/dstroy0/ProtoCore/commit/eb378e645c4ae03b51b0b4955af1b6bc5cbf547b))
- the member initializers the renamed files still carried ([`b87038c`](https://github.com/dstroy0/ProtoCore/commit/b87038c6306c112de285fa90b87514710561b9a2))
- convert the last ten src/ .cpp files except ssh_transport ([`667989a`](https://github.com/dstroy0/ProtoCore/commit/667989ac9e0be11e2d64b37313e371d39aeeabd5))
- drop the enum scope qualifiers and the last peripheral member initializers ([`499751f`](https://github.com/dstroy0/ProtoCore/commit/499751f19ae1b827a4ad2b4538be87e15984bab8))
- fifteen more src/ .cpp files that were already C become .c ([`59d01c2`](https://github.com/dstroy0/ProtoCore/commit/59d01c2f6df213ef8d6da041eccb31f70e6a48a7))
- the file-local helpers take their owning context by pointer ([`5e179c0`](https://github.com/dstroy0/ProtoCore/commit/5e179c06dcbac6975d367902b2974f05eb8b8e42))
- replace the C++ in-class member initializers ([`59b92b8`](https://github.com/dstroy0/ProtoCore/commit/59b92b887da8cc66cff56c8a6edaa96102bfc138))
- write the C++ temporaries as compound literals ([`1cdc9f6`](https://github.com/dstroy0/ProtoCore/commit/1cdc9f670d7f95cb6241c74cb76fc21232ac9c88))
- drop the DTLS ciphertext default arguments, state them at every call ([`e5cc867`](https://github.com/dstroy0/ProtoCore/commit/e5cc86712bd33a811514ce526048441e356a5cdc))
- spell the opaque handles as struct at every use ([`f509479`](https://github.com/dstroy0/ProtoCore/commit/f5094791006747f6adfa9721338f5d86ba3b925b))
- the eight src/ .cpp files that were already C become .c ([`ed97f45`](https://github.com/dstroy0/ProtoCore/commit/ed97f45d9a6d99725a2b4922496bb06814d52477))
- convert SecureScope, the DTLS key references, and the opaque GCM handles ([`4e1954c`](https://github.com/dstroy0/ProtoCore/commit/4e1954cd83fd6bfcd37d009e37dd79056ea6cbb3))
- replace the SecureBorrow RAII holder with the mark/span/release shape ([`8d94018`](https://github.com/dstroy0/ProtoCore/commit/8d94018e564f3a3163197bd6e3e6ca623a6221db))
- convert the HTTP client to C and unblock the edge-cache suites ([`01858c0`](https://github.com/dstroy0/ProtoCore/commit/01858c0792548477d91736b76df8048caafb4648))
- pass the config-store owner by pointer ([`640069f`](https://github.com/dstroy0/ProtoCore/commit/640069fe39d86a75bb2086ce37e81969caccd932))
- convert the web terminal to C ([`4d6f64f`](https://github.com/dstroy0/ProtoCore/commit/4d6f64ff8deedf87173471016bce5ab3a8b80b41))
- convert the chunked-send reference variables to pointers ([`a5d1823`](https://github.com/dstroy0/ProtoCore/commit/a5d18230f7d6685610e2dae97df0dd002bb502d9))
- convert the SSE module to C ([`a3f0813`](https://github.com/dstroy0/ProtoCore/commit/a3f08133fcd1fc661f9b1d76eeb5f004239a06f2))
- convert the WebSocket module to C ([`2de589f`](https://github.com/dstroy0/ProtoCore/commit/2de589f87f78709adb1ce31dc3b2534a8ec2a869))
- convert the multipart codec to C ([`4227997`](https://github.com/dstroy0/ProtoCore/commit/422799751a20dec48c6babedb9eebb0c7b7188a5))
- convert the HTTP parser to C ([`ef8dea4`](https://github.com/dstroy0/ProtoCore/commit/ef8dea48b351dce92bf1b33ab186c37dd31d0b18))
- convert the presentation layer entry to C ([`b01e8ac`](https://github.com/dstroy0/ProtoCore/commit/b01e8ac1ed4b8523f61fcf5667e565613da7f92b))
- move stdatomic into types.h ([`d7d2093`](https://github.com/dstroy0/ProtoCore/commit/d7d20933cb0772ecf9550cbd680d74b6bbecc2e2))
- convert the session layer to C ([`8eb49f4`](https://github.com/dstroy0/ProtoCore/commit/8eb49f4ad14b95c88dfac42c1da248671b3e595a))
- rename pentesting/ to penetration_testing/ and analyze the repo's Python ([`9cdad1a`](https://github.com/dstroy0/ProtoCore/commit/9cdad1a5e3f98b3e5de0e89b4a128be30697cfa9))
- drop the C++ default arguments from the SSH KDF entry points ([`ef9541c`](https://github.com/dstroy0/ProtoCore/commit/ef9541c495fdc482cf68b8cde8fcce830bb749e2))
- convert the remaining using-aliases to C typedefs ([`13ebde2`](https://github.com/dstroy0/ProtoCore/commit/13ebde29ad0b83d1e58559135c5639c5e1f50b2b))
- move the upload service onto the mnt storage seam ([`9a2bbaa`](https://github.com/dstroy0/ProtoCore/commit/9a2bbaa03e88b3a89c9797b89d1c38c77d784883))
- move mDNS, NTP, NTS and PTP into the L7 application layer ([`bcf041b`](https://github.com/dstroy0/ProtoCore/commit/bcf041b7358b324f34ddccceeae40be286a24161))
- restore internal linkage, bridge I2C, convert six files to C11 ([`ef26ac7`](https://github.com/dstroy0/ProtoCore/commit/ef26ac7ba100e631e7543645961de3865aca4785))
- split WebDAV into the L7 wire codec and the server handler ([`593e45a`](https://github.com/dstroy0/ProtoCore/commit/593e45a25d14a01599aa08017898d05c80acd7dc))
- move HTTP authentication into the L7 application layer ([`a602846`](https://github.com/dstroy0/ProtoCore/commit/a602846800937ce64a664fa8cb3f2f51c1a346ef))
- move the TLS 1.3 key schedule into network_drivers/tls ([`ba1f6f2`](https://github.com/dstroy0/ProtoCore/commit/ba1f6f213d0d9b7417c73e07445da63292c8475f))
- mirror the layer each module moved to ([`f575b3d`](https://github.com/dstroy0/ProtoCore/commit/f575b3dbfe9d40e243dbb67fffd804884cf2b59c))
- move exc_decoder and power_mgmt into server/ ([`d14ca1e`](https://github.com/dstroy0/ProtoCore/commit/d14ca1e69c88b656b593965e9b5cf1cc569dab97))
- move each module under the layer that owns it ([`bddf3f4`](https://github.com/dstroy0/ProtoCore/commit/bddf3f4a37136505974a95c4ae7054d78423c16e))

### Testing

- copy the trace samples at their real width ([`bc9bec9`](https://github.com/dstroy0/ProtoCore/commit/bc9bec9bbec75d2dff19cfcd09f9cd0c69a75f70))
- trace_capture reads its window through a pointer ([`33cc8a6`](https://github.com/dstroy0/ProtoCore/commit/33cc8a698d4cc6e778df5873e0f951233f8cf40d))
- gateway and trace_capture record into fixed tables ([`1c0133e`](https://github.com/dstroy0/ProtoCore/commit/1c0133e79af98df388fcadc2cbc67327a371d809))
- read the dma record payload directly ([`30c79d2`](https://github.com/dstroy0/ProtoCore/commit/30c79d234a154f7186a0e47bad919cab2b6e103e))
- dma records completions into a fixed table ([`b012d3c`](https://github.com/dstroy0/ProtoCore/commit/b012d3c7d89ba7272de18e6613088fa92a1c1128))
- the last ikev2 element assignment as a compound literal ([`60e8a41`](https://github.com/dstroy0/ProtoCore/commit/60e8a418765fbfb7df10520076396de9b479582e))
- ikev2's sized vectors become fixed arrays ([`28e7aed`](https://github.com/dstroy0/ProtoCore/commit/28e7aed7ee83b4e78b1b155ee1d270b770814b16))
- the KAT suite uses the keyed GCM handle and the explicit tag output ([`53c3d6d`](https://github.com/dstroy0/ProtoCore/commit/53c3d6d4c48b10ed61143490eb81b3c4b28da391))
- typedef the KAT structs, name ntlm's nibble lambda, restore SMB2_SIGN_ALGO_AES_CMAC ([`a06f4da`](https://github.com/dstroy0/ProtoCore/commit/a06f4dafac8ee69ceb81adf2cb65b11b432cffa2))
- name the hex-nibble lambdas, five more suites to C ([`45046ce`](https://github.com/dstroy0/ProtoCore/commit/45046ce30ccd636f7de8a6a0a83c8b33180b804f))
- call proto_tcp_conn_timeout_ms instead of comparing its address ([`569d2c7`](https://github.com/dstroy0/ProtoCore/commit/569d2c79d37b948cbe1a84ed65d381afd9845898))
- the transport helpers the qualifier strip left bare ([`6a3dbe6`](https://github.com/dstroy0/ProtoCore/commit/6a3dbe68dde65896e6558ab5f7ef1d0058875098))
- the host seam grows the one-shot failure hooks the transport suite drives ([`9ebb0c2`](https://github.com/dstroy0/ProtoCore/commit/9ebb0c28a52b45ca4dddf3eb6da2db5a6865d7d6))
- give the host seam the one-shot close failure, pass the worker id to the sweep ([`9d9d8be`](https://github.com/dstroy0/ProtoCore/commit/9d9d8bee9e43bddec1149291c9c2346403603483))
- transport reads the host seam's control-block and error types ([`28caacf`](https://github.com/dstroy0/ProtoCore/commit/28caacf194e170e3359045051bf3ca48362ffbe4))
- restore the std:: qualifiers the sweep wrongly stripped, convert statsd + spa_router ([`8c43357`](https://github.com/dstroy0/ProtoCore/commit/8c43357c7a1052a00d831491dc6bbcea3a0fedb4))
- drop the STL includes the C suites no longer need ([`710f311`](https://github.com/dstroy0/ProtoCore/commit/710f31109713796de41810e7f45bc28138a2108f))
- four more suites to C (coap, transport, spa_router, statsd) ([`e681d79`](https://github.com/dstroy0/ProtoCore/commit/e681d79b6b09730139edb99ebac8a1b05496636a))
- enable keep-alive in native_range ([`1267ccf`](https://github.com/dstroy0/ProtoCore/commit/1267ccf0daa8232d9c7050fd0afc0ea42146526f))
- mount the store test_range serves from ([`edfdef5`](https://github.com/dstroy0/ProtoCore/commit/edfdef596aca734b2091fa8cc8a7515d7195bed6))
- strip the verified scope qualifiers, typedef RamDisk ([`aef4dbf`](https://github.com/dstroy0/ProtoCore/commit/aef4dbf476cc26d6df788481229980065a8208ac))
- finish the six suites ([`ec21e1a`](https://github.com/dstroy0/ProtoCore/commit/ec21e1a9d4839ba2d7cf755884574e2da37de675))
- count aborts on the host seam ([`8b9e96e`](https://github.com/dstroy0/ProtoCore/commit/8b9e96ec4e11e307896f6a20e1e5f41208748ce7))
- serve test_range from the real filesystem instead of the Arduino FS mock ([`b9856b2`](https://github.com/dstroy0/ProtoCore/commit/b9856b2a0aa96e0bc13ab96cf0cf2d9127ae4382))
- fixed accumulators in place of the vectors, six more suites to C ([`fbbd051`](https://github.com/dstroy0/ProtoCore/commit/fbbd051fd0f188b7cebb80a990d2347f28e5f085))
- sweep the mechanical C++ tokens out of the suites ([`53cfef4`](https://github.com/dstroy0/ProtoCore/commit/53cfef46e0a9ec83f19267432e10187ca42ea4f0))
- hoist the dns_server resolver callback to file scope ([`e73f033`](https://github.com/dstroy0/ProtoCore/commit/e73f03367f67569aa6fdbec2415097167fa70413))
- name the lambdas and drop the heap from three suites ([`72867d5`](https://github.com/dstroy0/ProtoCore/commit/72867d518ed097cba91d6000240a89fd1fc2f65c))
- use the transport layer's C names ([`111f9cc`](https://github.com/dstroy0/ProtoCore/commit/111f9ccc92c5b7971bf4db37dcac871e7b9286e2))
- index the hpack roundtrip table instead of a range-for ([`8f4993c`](https://github.com/dstroy0/ProtoCore/commit/8f4993cf2dab77c437671ad8531f43b18fcdc69a))
- finish the tier-1 suite conversions ([`423c920`](https://github.com/dstroy0/ProtoCore/commit/423c920b299416c1500cd43887a362304f141820))
- convert the low-residue suites from .cpp to .c ([`b73b584`](https://github.com/dstroy0/ProtoCore/commit/b73b5846fdff32369d34d5fb51e8be5ccb673c4a))
- strip the default arguments from the suite-local helpers ([`b64541a`](https://github.com/dstroy0/ProtoCore/commit/b64541a288f0d11174db4bcb68d9f47d1ff68553))
- take the address at the remaining DTLS record-key call sites ([`ff461f5`](https://github.com/dstroy0/ProtoCore/commit/ff461f50c8fb9b11134ebaf039b5bf7bb2fe7f78))
- pass the DTLS record keys by address now that the parameter is a pointer ([`e696260`](https://github.com/dstroy0/ProtoCore/commit/e696260e60ba2f301f62845f39297dd64f27b8f6))
- restore the DtlsCipher member prefix and the explicit AEAD tag argument ([`ba56fd8`](https://github.com/dstroy0/ProtoCore/commit/ba56fd8a8fead8362e579d03e574175c2afd09b9))
- enable SSE in native_sse, drop the lwIP mock from two host suites ([`a22dd37`](https://github.com/dstroy0/ProtoCore/commit/a22dd37a4e717d3b321418a91b27ffb3762c86c8))
- stop tcp_capture_disable() from wiping the capture, build arena.c for native_workers ([`227973e`](https://github.com/dstroy0/ProtoCore/commit/227973e458c65403fd56c0501f71f7b86d90b499))
- give the host seam the write-failure hook the lwIP mock owned ([`b763ab2`](https://github.com/dstroy0/ProtoCore/commit/b763ab20740765865c0e261cf53102fdeb92b3eb))
- convert the workers suite to C ([`b8ad778`](https://github.com/dstroy0/ProtoCore/commit/b8ad7784d14fa3e5cf36a78fde9f23233f3998cc))
- drop the default arguments from build_v3_raw_scoped ([`caae75e`](https://github.com/dstroy0/ProtoCore/commit/caae75e08551e7fd2b2cde50023e23a747505f91))
- restore the SnmpTag member prefix across the snmp suites ([`3a3decc`](https://github.com/dstroy0/ProtoCore/commit/3a3decc4aeed188ae130be8d31a73c25be5ec3af))
- typedef the struct tags the scope strip left bare ([`2b43c6e`](https://github.com/dstroy0/ProtoCore/commit/2b43c6e9f7d8479558f30bf4a14516eae8b95854))
- latch the PUT error on a real mid-stream refusal ([`79215a3`](https://github.com/dstroy0/ProtoCore/commit/79215a3ce2076ab54bb749e11ffc9516642a34c9))
- refuse the abort-path PUT through the store, not a node table ([`098343d`](https://github.com/dstroy0/ProtoCore/commit/098343d7fbabad95aa9c1299ed45496f71b4df9f))
- COPY onto the root collection answers 409, observed not assumed ([`bd32327`](https://github.com/dstroy0/ProtoCore/commit/bd323271215a809a4bdde7e519b5f5083f5b1fe4))
- temporary diagnostic on the COPY-onto-root response ([`422f1b4`](https://github.com/dstroy0/ProtoCore/commit/422f1b467fd011266ff4cabd931d6e05e49cafd0))
- copying onto the root collection is refused, not created ([`124b6b7`](https://github.com/dstroy0/ProtoCore/commit/124b6b75596ee408e73290d8eb5c75e07c0c2bf8))
- a real filesystem always has a root, so the bare mount resolves ([`94115b5`](https://github.com/dstroy0/ProtoCore/commit/94115b5a9d94ce422625978eafdd11e0cfe12fda))
- stop a WebDAV delete through the write it needs, not a handle ([`28620ea`](https://github.com/dstroy0/ProtoCore/commit/28620eaa287a894899e8b7a61f1a3d6b8874c315))
- add a medium that refuses every write ([`1345032`](https://github.com/dstroy0/ProtoCore/commit/134503202fadcac46a6631aa63ff438efbbe032e))
- WebDAV storage refusals now come from the medium ([`5b0624b`](https://github.com/dstroy0/ProtoCore/commit/5b0624b57dc61f025c534d06aeb3b9454465fd86))
- refuse through the medium instead of through exhaustion ([`5f6a8ce`](https://github.com/dstroy0/ProtoCore/commit/5f6a8ce64c12f3d9126914463e1677ba9330c71f))
- remount after filling, so the fixture starts from the medium ([`86bcc96`](https://github.com/dstroy0/ProtoCore/commit/86bcc964e5d7db807d58c780664ab70d4c5f0385))
- measure whether a larger fixture volume survives exhaustion ([`b6eca8a`](https://github.com/dstroy0/ProtoCore/commit/b6eca8abd918d8583fa12213e1786d28a12453cf))
- check the store still answers after a full fill ([`464b6ee`](https://github.com/dstroy0/ProtoCore/commit/464b6ee3336650d3cef1bac9e03a2d9b0d567140))
- check the fixture supports the concurrent handles a COPY needs ([`ab2e29a`](https://github.com/dstroy0/ProtoCore/commit/ab2e29aff032a64a10825916827f915af1b9e10f))
- use the proven fill for the remaining WebDAV creation-refused cases ([`4d3376b`](https://github.com/dstroy0/ProtoCore/commit/4d3376bfa80fa3b7820f3d3565bf9098cb11f5aa))
- prove the fill helper actually exhausts the store ([`00c4ca1`](https://github.com/dstroy0/ProtoCore/commit/00c4ca106290ba6dd5caade7b917632acf7ce508))
- fill the volume for the copy-destination-refused case ([`3c89931`](https://github.com/dstroy0/ProtoCore/commit/3c899315cadd140e0b81b44daaf49edc564fe2d4))
- leave headroom so the 507 case fails on the write, not the open ([`db8fbe1`](https://github.com/dstroy0/ProtoCore/commit/db8fbe1c19f25bfb25717c3be6504c29f95dd7c5))
- express the WebDAV storage-failure cases as real conditions ([`11eb59a`](https://github.com/dstroy0/ProtoCore/commit/11eb59a5bec6cafa84e3cbb46ad17eaca1789cd2))
- create the WebDAV mount root before serving from it ([`8735ff7`](https://github.com/dstroy0/ProtoCore/commit/8735ff7cd3ce59ac0b54858568473529103ea9db))
- force the WebDAV failure paths by causing them, not flagging them ([`6d0cbf0`](https://github.com/dstroy0/ProtoCore/commit/6d0cbf046afca4414ad65000775391a0ee679f35))
- create the collections above a fixture write ([`6bf5651`](https://github.com/dstroy0/ProtoCore/commit/6bf56513b22ae4b9633555cd08a6b96125263a9d))
- build the route table and signaling for the WebDAV suite ([`4b5d1cf`](https://github.com/dstroy0/ProtoCore/commit/4b5d1cfdda3885fe32b5842062e57d2a435398e8))
- build the accessor and mount seam for the WebDAV suite ([`c17d8bb`](https://github.com/dstroy0/ProtoCore/commit/c17d8bba6c0c273dbb606344f1239145582e57b1))
- include the littlefs fixture in the WebDAV suite ([`0d0407b`](https://github.com/dstroy0/ProtoCore/commit/0d0407b5fd127d9e6cc50b1598ff8a72a30ad7bf))
- move the WebDAV suite onto the littlefs fixture ([`4a11475`](https://github.com/dstroy0/ProtoCore/commit/4a11475068f4381fa9ce4737ff2fa7573d5c3abc))
- back the host mount fixture with real littlefs ([`26c6ba4`](https://github.com/dstroy0/ProtoCore/commit/26c6ba4781b24dfa857a5de7f791eef60796af46))
- depend on littlefs, the filesystem the device runs ([`ec4ec8b`](https://github.com/dstroy0/ProtoCore/commit/ec4ec8be659e56e0b7d7e2b998a94e504bcccdf6))
- build the signaling TU for the upload suite ([`8f0555f`](https://github.com/dstroy0/ProtoCore/commit/8f0555f811c3c2dfaf1e2acd91aef2d06c74cd1e))
- build the route table and mount seam for the upload suite ([`21f6be5`](https://github.com/dstroy0/ProtoCore/commit/21f6be5031001e3516c19c86a4141bbd27edea8d))
- move the upload suite onto the mount seam ([`74f9f78`](https://github.com/dstroy0/ProtoCore/commit/74f9f785a2d85804ac1880e7a0d4489ddf73b8bd))
- restore the SCP mode names after the scope strip ([`48888d8`](https://github.com/dstroy0/ProtoCore/commit/48888d81cb5850f95bf6e002a785f081ff0963b7))
- restore the hotswap state names after the scope strip ([`9f745df`](https://github.com/dstroy0/ProtoCore/commit/9f745df97b836b0288162dc10633858fae28c12f))
- size the host send capture for a whole multi-window response ([`392d378`](https://github.com/dstroy0/ProtoCore/commit/392d378ee9ce3c84cfd006564baf3730a944de50))
- mount the file-serving fixture ([`0441bd0`](https://github.com/dstroy0/ProtoCore/commit/0441bd00f77aedb1e2bcd28beb3fafe2b6e2236c))
- build the filesystem accessor and mount seam in the http stack ([`a528ad4`](https://github.com/dstroy0/ProtoCore/commit/a528ad4843b731355ec48be23b4f781c34226fc0))
- enable PC_ENABLE_FILE_SERVING for its own suite ([`94413d8`](https://github.com/dstroy0/ProtoCore/commit/94413d8adda9074e792f697c9d23ef42e5e52cbd))
- set the mock send buffer through its setter ([`b4b9a64`](https://github.com/dstroy0/ProtoCore/commit/b4b9a644f5d4aefb773f685d646143cb84c8a965))
- give the hoisted handlers their state and the defaulted mtime ([`d7152c2`](https://github.com/dstroy0/ProtoCore/commit/d7152c2db1efd2b0633790abd1cc5a5b8f8ac5d6))
- hoist the file-serving handlers out of lambdas ([`22a0087`](https://github.com/dstroy0/ProtoCore/commit/22a0087ee5815de5a9663bb39e5568c10be3a3b4))
- give the host a pc_mnt_backend fixture and move file serving onto it ([`18a0c36`](https://github.com/dstroy0/ProtoCore/commit/18a0c3620c2ac0c7465fcfa238f28f4f772efc4d))
- use on_http_iface for the interface-scoped overload ([`93920a6`](https://github.com/dstroy0/ProtoCore/commit/93920a68af6578f64065697a23a592fec2a9adf7))
- spell the infinite loop without the C++ keyword ([`030b761`](https://github.com/dstroy0/ProtoCore/commit/030b761011044df82440ba3aa3377b5ce6666bc0))
- convert the defer and presentation suites to C ([`b0d3177`](https://github.com/dstroy0/ProtoCore/commit/b0d317764278d3810e6ac4e54ce5f1fc8618c72b))
- enable PC_ENABLE_AUTH for the auth suites ([`102cc67`](https://github.com/dstroy0/ProtoCore/commit/102cc67f86b910c47b3c54d97976f1c842486a52))
- finish the auth arity and give the host seam a settable send buffer ([`84a9c6c`](https://github.com/dstroy0/ProtoCore/commit/84a9c6c244e56e17c063685267ba3dff4d84f906))
- match the arities C left behind ([`201dcb5`](https://github.com/dstroy0/ProtoCore/commit/201dcb52a4faf6d9294c4b679bc35677a1ee80d6))
- convert the remaining JSON writers to the C API ([`2900695`](https://github.com/dstroy0/ProtoCore/commit/2900695432d60a30797e4e5ead31ae25a7c2fe77))
- move the JSON suite onto the C writer API ([`5e95259`](https://github.com/dstroy0/ProtoCore/commit/5e95259d88120cffb5a277be8c13c1fb03ff1adc))
- give the host seam the send-capture accessors, and build route + signaling ([`25435d5`](https://github.com/dstroy0/ProtoCore/commit/25435d52b992c3f2478acb0d581b699335d03eed))
- bind slots through the host seam and fix the value-init assignments ([`fa14afe`](https://github.com/dstroy0/ProtoCore/commit/fa14afe4ef97d3cab6f198ba86bddcbceb0148c9))
- enable the dependencies the feature gates require ([`c7e7725`](https://github.com/dstroy0/ProtoCore/commit/c7e77251b877096ebfc4c22ecb47bd2171f84697))
- pass the tls flag listener_add no longer defaults ([`d8048bc`](https://github.com/dstroy0/ProtoCore/commit/d8048bcc63166e41d550c397146de2aa9a0b1620))
- convert test_session to C ([`6d760e4`](https://github.com/dstroy0/ProtoCore/commit/6d760e4f8d148680faf649b7b873850ae8852bab))
- convert test_forward to C ([`cd51377`](https://github.com/dstroy0/ProtoCore/commit/cd5137768304755897491ed960dfa40bb7306294))
- build the clock TU in native_clock ([`5ab8253`](https://github.com/dstroy0/ProtoCore/commit/5ab82534bf6a084ab9ee782de6a1a7b3bed4835c))
- make the host mocks compile as C and restore the virtual clock ([`940c3fd`](https://github.com/dstroy0/ProtoCore/commit/940c3fdd966f46480dc0e866055008ad324271c9))
- restore packml enum member names after the C++ scope strip ([`cfd42e8`](https://github.com/dstroy0/ProtoCore/commit/cfd42e850b9394a48c54c708fe4377f5be82a203))
- give the remaining 134 suites the library's own truth type ([`3661181`](https://github.com/dstroy0/ProtoCore/commit/3661181b56353ee44717595f3e3c3f91b5c9a43c))
- fix a missed ks_handshake call site and the Ext case table ([`70f03b6`](https://github.com/dstroy0/ProtoCore/commit/70f03b6984e9d3ed70e9fad597cced9e68fa1e30))
- spell the local case-table structs as typedefs in test_tls13_msg ([`98641c1`](https://github.com/dstroy0/ProtoCore/commit/98641c125944c5dfb017a9aaf9d98058e72b3d1a))
- give the QUIC/TLS suites the library's own truth type ([`c68d9a8`](https://github.com/dstroy0/ProtoCore/commit/c68d9a8adfbe8072b2418e20fe40a8b4513997c8))
- convert the mechanically-convertible suites to C11 ([`79e56d1`](https://github.com/dstroy0/ProtoCore/commit/79e56d1187c33e042ef04f3370e3d2fb17762a8c))
- assert the six enum widths at compile time ([`4ef3417`](https://github.com/dstroy0/ProtoCore/commit/4ef34170f4da2ebfc929917337b8d566ddc24724))

</details>

## [0.0.7] - 2026-08-02

<details>
<summary><b>Show Changelog for version 0.0.7 - 2026-08-02</b></summary>

### CI / Build

- update CHANGELOG.md [skip ci] ([`10be3c0`](https://github.com/dstroy0/ProtoCore/commit/10be3c0340eef80f9a8e417435372a7656e20c34))

### Changes

- Bump version: 0.0.6 → 0.0.7 ([`4a6ac90`](https://github.com/dstroy0/ProtoCore/commit/4a6ac90370aaad684ff0656dacefe860615f667d))

### Documentation

- state C11 in the law, and retire DONE as a status in the sweep notes ([`0a52923`](https://github.com/dstroy0/ProtoCore/commit/0a529237c945b81248cbce7198770475a65790ec))

</details>

## [0.0.6] - 2026-08-02

<details>
<summary><b>Show Changelog for version 0.0.6 - 2026-08-02</b></summary>

### Bug Fixes

- convert the C++ type declarations behind sqlite, aes128gcm and hkdf ([`a437340`](https://github.com/dstroy0/ProtoCore/commit/a437340764f7dc33866768552b8272287914ef67))

### CI / Build

- update CHANGELOG.md [skip ci] ([`49b3d78`](https://github.com/dstroy0/ProtoCore/commit/49b3d7834c6f6ba1508d112caaf1771579e4e1a4))

### Changes

- Bump version: 0.0.5 → 0.0.6 ([`0b8de8e`](https://github.com/dstroy0/ProtoCore/commit/0b8de8e038484f35d0864a0595895ae7c966d014))

### Documentation

- regenerate feature tables + configurator + build_opt.h + example index [skip ci] ([`90e44e7`](https://github.com/dstroy0/ProtoCore/commit/90e44e76e787703bf942a4168e5b25b7fd2302f0))

</details>

## [0.0.5] - 2026-08-02

<details>
<summary><b>Show Changelog for version 0.0.5 - 2026-08-02</b></summary>

### Bug Fixes

- restore the constant names the constexpr sweep truncated, and close the guard chain ([`0e46ddf`](https://github.com/dstroy0/ProtoCore/commit/0e46ddff0c913c41cb86bda959a5e35c83960fe8))

### CI / Build

- update test report + coverage [skip ci] ([`10dfc51`](https://github.com/dstroy0/ProtoCore/commit/10dfc517fcb3c4caad3f2566fb3c1121c482021a))
- update CHANGELOG.md [skip ci] ([`b30eabf`](https://github.com/dstroy0/ProtoCore/commit/b30eabf2e5a8e008c58b1f9af9c820655df06b74))

### Changes

- Bump version: 0.0.4 → 0.0.5 ([`df66a22`](https://github.com/dstroy0/ProtoCore/commit/df66a22c04544a452ddb3ba16b8e87f8bd55e649))

### Documentation

- regenerate feature tables + configurator + build_opt.h + example index [skip ci] ([`ab43d04`](https://github.com/dstroy0/ProtoCore/commit/ab43d047b2a0dd1465890580c10c5d341d0be9a1))

</details>

## [0.0.4] - 2026-08-02

<details>
<summary><b>Show Changelog for version 0.0.4 - 2026-08-02</b></summary>

### Bug Fixes

- reattach the three envs left extending a base the rename deleted ([`e7cb3db`](https://github.com/dstroy0/ProtoCore/commit/e7cb3db79ce725ceb5ca76270fc4a105a858a9fb))
- pin LF in every generator that writes a text file ([`2573320`](https://github.com/dstroy0/ProtoCore/commit/2573320328b0b5065c89b84c63493073ef6eea95))
- unbreak the default link, and stop the tree walk storing every path ten times ([`2602f75`](https://github.com/dstroy0/ProtoCore/commit/2602f75c7cfc1f34b9957912724a4479fa7ae40e))
- stop protocore.h from defining a secret and declaring six symbols nobody can link ([`e8e2853`](https://github.com/dstroy0/ProtoCore/commit/e8e28535ae1defd11b5139d3b17eb619f102a95e))
- native_ssh satisfies the SFTP/SCP guard through the mount, not FILE_SERVING ([`8915c33`](https://github.com/dstroy0/ProtoCore/commit/8915c330d25e0e8380a8aeabfc89d601744cfdd6))

### CI / Build

- update test report + coverage [skip ci] ([`5cedaba`](https://github.com/dstroy0/ProtoCore/commit/5cedabacd81b1338b94919c959bba2eaf2d39462))
- update CHANGELOG.md [skip ci] ([`85312bf`](https://github.com/dstroy0/ProtoCore/commit/85312bf9608bb253d7f4cca9e50d8d70a401acdb))
- update test report + coverage [skip ci] ([`886bd8a`](https://github.com/dstroy0/ProtoCore/commit/886bd8ac256fde8928df1188508c7fae6d250a5a))
- update CHANGELOG.md [skip ci] ([`260ca5a`](https://github.com/dstroy0/ProtoCore/commit/260ca5aa5f608d221939b6cadcf84b57bb1e85bf))
- update CHANGELOG.md [skip ci] ([`0194c7d`](https://github.com/dstroy0/ProtoCore/commit/0194c7d68334fe635c7936e64a5eefcb663645a7))
- update CHANGELOG.md [skip ci] ([`1e4a815`](https://github.com/dstroy0/ProtoCore/commit/1e4a8158fe0de45819d4f9c010d685d68271025d))
- update CHANGELOG.md [skip ci] ([`3316610`](https://github.com/dstroy0/ProtoCore/commit/3316610af8f086b8ce7a2190ea74f0c55b47d790))
- update test report + coverage [skip ci] ([`b611a4f`](https://github.com/dstroy0/ProtoCore/commit/b611a4f39c498d21f4ec36ff2284b8d9c2ca11db))
- update CHANGELOG.md [skip ci] ([`8112685`](https://github.com/dstroy0/ProtoCore/commit/81126853eb347164c0712372c63ab1c8c3e075f4))
- update test report + coverage [skip ci] ([`5e287f6`](https://github.com/dstroy0/ProtoCore/commit/5e287f63b628c8d0a26c22d2c84bb410aa7b983f))
- update CHANGELOG.md [skip ci] ([`56662c1`](https://github.com/dstroy0/ProtoCore/commit/56662c18f43fc49ae7d4b0b5b57d20fcdb3cc4de))

### Changes

- Bump version: 0.0.3 → 0.0.4 ([`4619ab9`](https://github.com/dstroy0/ProtoCore/commit/4619ab9624c47d45d0d5d73ca896c87fffd57e06))
- Merge remote-tracking branch 'origin/main' into refactor/lib-wide ([`dd7a7a1`](https://github.com/dstroy0/ProtoCore/commit/dd7a7a1e354b12feee9b5365653e217b872fd0fe))
- Merge pull request #20 from dstroy0/refactor/json-codec ([`3103251`](https://github.com/dstroy0/ProtoCore/commit/3103251059e3e69ff6dca2ab00a27367a1d38903))
- Merge remote-tracking branch 'origin/main' into refactor/json-codec ([`f12ca3f`](https://github.com/dstroy0/ProtoCore/commit/f12ca3f301fd878bf606ad34c2c9b6800e24ae14))

### Documentation

- regenerate feature tables + configurator + build_opt.h + example index [skip ci] ([`27a9294`](https://github.com/dstroy0/ProtoCore/commit/27a9294173dc9856b338196465b7002f591457ba))
- move the CI badges to the top of the README ([`75a9345`](https://github.com/dstroy0/ProtoCore/commit/75a934560c4e55760f081b7402c3e128af89e705))
- regenerate feature tables + configurator + build_opt.h + example index [skip ci] ([`9346b62`](https://github.com/dstroy0/ProtoCore/commit/9346b6247809ad826fe7134eb114167885260aa9))
- publish features.html and the diagrams, and cut the README down ([`5dca79b`](https://github.com/dstroy0/ProtoCore/commit/5dca79bbf3c8d2fa038096836bf35965274a7aa5))
- update ESP32 build footprints [skip ci] ([`f2dcdce`](https://github.com/dstroy0/ProtoCore/commit/f2dcdce98fcfad7be3714b3638f4e957f82bdd3b))
- regenerate feature tables + configurator + build_opt.h + example index [skip ci] ([`181b870`](https://github.com/dstroy0/ProtoCore/commit/181b8709c1c90b98d206536a9894c2b5f15aa5eb))
- make every badge a link ([`31fb989`](https://github.com/dstroy0/ProtoCore/commit/31fb989410d349c781791b42064dc799e6ec1f62))
- interactive SVG diagrams, and a README that is not half feature table ([`a452865`](https://github.com/dstroy0/ProtoCore/commit/a4528657ee22a61959d1a62056aa37bb354d292b))
- update ESP32 build footprints [skip ci] ([`3f08774`](https://github.com/dstroy0/ProtoCore/commit/3f087746400255b493ec6e3aa557c95ea05ea91d))
- regenerate feature tables + configurator + build_opt.h + example index [skip ci] ([`8f56a71`](https://github.com/dstroy0/ProtoCore/commit/8f56a711ead1789f8e9725692203e260679442e5))

### Features

- give the server's state one place to be read from ([`10b5eba`](https://github.com/dstroy0/ProtoCore/commit/10b5ebad902d6687790fb7fb60fe7775f8b7ea0f))

### Refactor

- convert src/ to C11 and split the build into PROTOCORE_HOT / PROTOCORE_HOST ([`40d217a`](https://github.com/dstroy0/ProtoCore/commit/40d217a7272b721e83230cb1ca6d0517622824e2))
- split swar into an access layer and bounded-run operations ([`d5d947e`](https://github.com/dstroy0/ProtoCore/commit/d5d947ed28b48109556e8bce14e5fbeb55a80f90))
- finish removing the PC class, and default every feature off ([`65a3886`](https://github.com/dstroy0/ProtoCore/commit/65a3886a0bd147bcc5d039fab5243a86db53ce4a))
- give the filesystem accessor the tree operations, and mnt back its blindness ([`09227b6`](https://github.com/dstroy0/ProtoCore/commit/09227b6ca482d960510a2215af1ed390e118d98b))
- delete the PC class and give the vfs/mnt split its boundary back ([`8b089bb`](https://github.com/dstroy0/ProtoCore/commit/8b089bb8a32bc00f7debb6a512f11d90b5766975))

</details>

## [0.0.3] - 2026-07-31

<details>
<summary><b>Show Changelog for version 0.0.3 - 2026-07-31</b></summary>

### Bug Fixes

- reject a wire length that overflows the bounds check on 32-bit targets ([`ae8cad2`](https://github.com/dstroy0/ProtoCore/commit/ae8cad246f551671d7abadce663661b6013e0b41))
- derive forced feature dependencies instead of rewriting the user's flags ([`88e22b3`](https://github.com/dstroy0/ProtoCore/commit/88e22b35d38ded040b53fecc01709db306d4e781))

### CI / Build

- update test report + coverage [skip ci] ([`00811fb`](https://github.com/dstroy0/ProtoCore/commit/00811fbbd5dc719beede97f8b4dbc47d1c005e6e))
- update CHANGELOG.md [skip ci] ([`8a943ec`](https://github.com/dstroy0/ProtoCore/commit/8a943ec926367200f44e7d39f9d5126e6176a8b5))
- update test report + coverage [skip ci] ([`1e9cdd5`](https://github.com/dstroy0/ProtoCore/commit/1e9cdd5077d17b62a1ec90af9119cd15886b7a11))
- update CHANGELOG.md [skip ci] ([`50caf1f`](https://github.com/dstroy0/ProtoCore/commit/50caf1f27267d49d3757e7161f38965ac6ec7ba9))
- update CHANGELOG.md [skip ci] ([`72224c1`](https://github.com/dstroy0/ProtoCore/commit/72224c15544832a701832fd87e966c0f5e3b2946))
- update CHANGELOG.md [skip ci] ([`6752e98`](https://github.com/dstroy0/ProtoCore/commit/6752e9892986a0af0c0b2b839332a09bfbb59c20))
- update test report + coverage [skip ci] ([`ad4777c`](https://github.com/dstroy0/ProtoCore/commit/ad4777cef85e1f702b1db9f7ea2750af9bc3db21))
- update CHANGELOG.md [skip ci] ([`e81702c`](https://github.com/dstroy0/ProtoCore/commit/e81702cdcef51bcb55014bd788de04ae0695f02c))
- update CHANGELOG.md [skip ci] ([`6c1786d`](https://github.com/dstroy0/ProtoCore/commit/6c1786d3ef72d038363604de3955417603691975))
- key the banned-construct baseline by a normalized path ([`ea8e022`](https://github.com/dstroy0/ProtoCore/commit/ea8e022bdfcf4692b9b62aa5abc8d1867fdc1c07))
- update CHANGELOG.md [skip ci] ([`38580e4`](https://github.com/dstroy0/ProtoCore/commit/38580e4359b5665e17d76ee3c38512ab0a46ed9d))
- update CHANGELOG.md [skip ci] ([`86aa6cb`](https://github.com/dstroy0/ProtoCore/commit/86aa6cba9f4c5fa86416fe7bea5b2d264c07a796))
- update CHANGELOG.md [skip ci] ([`ea03093`](https://github.com/dstroy0/ProtoCore/commit/ea0309343e7dbee65ed6524c3d55d9b9c17767d7))
- update CHANGELOG.md [skip ci] ([`e18b9d7`](https://github.com/dstroy0/ProtoCore/commit/e18b9d76f067f11c98f3177f91d67cb6cc0830c4))
- update test report + coverage [skip ci] ([`52e932c`](https://github.com/dstroy0/ProtoCore/commit/52e932cffc41fca5cbe67cec5e72859150e41b8b))
- update CHANGELOG.md [skip ci] ([`b606b1e`](https://github.com/dstroy0/ProtoCore/commit/b606b1e87f8eff401f14f7cb6d19707d7da6da0f))
- fix the web.h guard, exempt the generated blob, justify one enum ([`c969bf4`](https://github.com/dstroy0/ProtoCore/commit/c969bf4b92e5c443a508d6030c09f420e3ac5f0d))
- update CHANGELOG.md [skip ci] ([`0eddf11`](https://github.com/dstroy0/ProtoCore/commit/0eddf11e583f235ecfa7a44f3a8844eaac62e798))
- resolve 12 stale doc citations ([`d20daf7`](https://github.com/dstroy0/ProtoCore/commit/d20daf7578d7259a91fe220734833d38c68a4d03))
- update CHANGELOG.md [skip ci] ([`555b319`](https://github.com/dstroy0/ProtoCore/commit/555b319fc2a334cee9d0b9566412e5c123635fa1))
- fix the gates that broke on the fresh tree ([`4d3715b`](https://github.com/dstroy0/ProtoCore/commit/4d3715b2ab71b46179829c39e6d664fdd63f2286))
- update CHANGELOG.md [skip ci] ([`04abcc2`](https://github.com/dstroy0/ProtoCore/commit/04abcc239d9d401b4de7ae06f667fcd40b469f33))

### Changes

- Bump version: 0.0.2 → 0.0.3 ([`bb17397`](https://github.com/dstroy0/ProtoCore/commit/bb173979946afe7c85ee83f7bbe27ef84d16f81b))
- Bump version: 0.0.1 → 0.0.2 ([`2c6672b`](https://github.com/dstroy0/ProtoCore/commit/2c6672bdd72389bf9903f822b4b94ed2a07434cd))
- drop the clip mode; logging takes the one contract ([`6195264`](https://github.com/dstroy0/ProtoCore/commit/61952644c3afebec52a40acce1bf4014ec74c3e9))
- delete the duplicate web_assets copy that broke every example link ([`7df281f`](https://github.com/dstroy0/ProtoCore/commit/7df281fc16d7d5903427fcfcbbe5d31bb4e1de04))
- close ban 20 - the three printf APIs take a frame spec ([`6505d73`](https://github.com/dstroy0/ProtoCore/commit/6505d73ed0ce76b23cf10fefca3c8f5eef5a6492))
- build the last fixed-shape frames with pc_sb, not snprintf ([`114a275`](https://github.com/dstroy0/ProtoCore/commit/114a2752802ef2fbbd228412cee88f0026bd004f))
- ban nondeterministic dispatch, retire the last format-string appender ([`210dd35`](https://github.com/dstroy0/ProtoCore/commit/210dd35746cb481abed5713bab282dbad1d24a93))
- pin LF checkout on every platform ([`692024f`](https://github.com/dstroy0/ProtoCore/commit/692024f92ae597f2cfd45906c24dcd714914b4d7))

### Documentation

- regenerate feature tables + configurator + build_opt.h + example index [skip ci] ([`7f8fed4`](https://github.com/dstroy0/ProtoCore/commit/7f8fed4f82b76d6847e7ec2e6e171d70eaf47b6b))
- update ESP32 build footprints [skip ci] ([`c80866d`](https://github.com/dstroy0/ProtoCore/commit/c80866d1aefc157773679d956603bb86a9fe639d))
- regenerate feature tables + configurator + build_opt.h + example index [skip ci] ([`5b76ba0`](https://github.com/dstroy0/ProtoCore/commit/5b76ba0de40bc3510ac33c1bed586265695c59c9))
- regenerate feature tables + configurator + build_opt.h + example index [skip ci] ([`b79f161`](https://github.com/dstroy0/ProtoCore/commit/b79f161c7b4c3b5e41056063c2f3e3c20eb72f9b))
- update ESP32 build footprints [skip ci] ([`2389946`](https://github.com/dstroy0/ProtoCore/commit/2389946a22a33f22da115887ba9bf65381a08ea6))
- regenerate feature tables + configurator + build_opt.h + example index [skip ci] ([`1d9135c`](https://github.com/dstroy0/ProtoCore/commit/1d9135c455fdf2156d4b8646619d449d480aa6e1))
- update ESP32 build footprints [skip ci] ([`61e6e03`](https://github.com/dstroy0/ProtoCore/commit/61e6e03ee6d3c50dc2b01b2f4e7d3a71ec9772c4))
- regenerate feature tables + configurator + build_opt.h + example index [skip ci] ([`fae0bdb`](https://github.com/dstroy0/ProtoCore/commit/fae0bdb29c5fc8a935801b79066778cab79eb778))
- close out the Sphinx entries in the delivery record ([`49ea705`](https://github.com/dstroy0/ProtoCore/commit/49ea705b0d4c4be6f0dbe7723e4d484ad10259cf))
- rebuild the Doxygen theme, group the sidebar, drop the Sphinx site ([`8cf35ee`](https://github.com/dstroy0/ProtoCore/commit/8cf35ee5f5ad540bc23b5dc029ded3d7fb07e4de))
- regenerate feature tables + configurator + build_opt.h + example index [skip ci] ([`486bef5`](https://github.com/dstroy0/ProtoCore/commit/486bef5a408d99ef45f469d26d2b34a105c1f557))
- update ESP32 build footprints [skip ci] ([`494a04f`](https://github.com/dstroy0/ProtoCore/commit/494a04f85f24c9ddc497385c6354d69fc939236f))
- regenerate feature tables + configurator + build_opt.h + example index [skip ci] ([`10bac7c`](https://github.com/dstroy0/ProtoCore/commit/10bac7c2fc8188fe2ee61dba79398f869427c5c1))

### Refactor

- give storage one owner and take the vendor out of the file-transfer servers ([`5081912`](https://github.com/dstroy0/ProtoCore/commit/5081912b66c3d24aa60b61ca3ed57d462e9f0a36))
- take the codec's tag byte out of the shared read cursor ([`d4956d8`](https://github.com/dstroy0/ProtoCore/commit/d4956d85f4a3382d7ddfccf836bf44c33e32bef7))
- collapse the codec cursors onto pc_span and give SSH signaling an owner ([`e2d0b4e`](https://github.com/dstroy0/ProtoCore/commit/e2d0b4e7a104053a6135ca68dc7955ed59fa9687))

</details>

## [0.0.1] - 2026-07-31

<details>
<summary><b>Show Changelog for version 0.0.1 - 2026-07-31</b></summary>

### Changes

- ProtoCore 0.0.1 ([`dfc3436`](https://github.com/dstroy0/ProtoCore/commit/dfc343615028920abe5045f94e57b2012b273675))

</details>
