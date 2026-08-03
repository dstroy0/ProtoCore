# Changelog

All notable changes to ProtoCore are documented here.

## [Unreleased]

### Bug Fixes

- make the checkers agree with a C11 tree ([`0c2bd52`](https://github.com/dstroy0/ProtoCore/commit/0c2bd52a962b3a93b6ce865868debb3cb3089aaa))
- convert the C++ type declarations behind sqlite, aes128gcm and hkdf ([`a437340`](https://github.com/dstroy0/ProtoCore/commit/a437340764f7dc33866768552b8272287914ef67))
- restore the constant names the constexpr sweep truncated, and close the guard chain ([`0e46ddf`](https://github.com/dstroy0/ProtoCore/commit/0e46ddff0c913c41cb86bda959a5e35c83960fe8))
- reattach the three envs left extending a base the rename deleted ([`e7cb3db`](https://github.com/dstroy0/ProtoCore/commit/e7cb3db79ce725ceb5ca76270fc4a105a858a9fb))
- pin LF in every generator that writes a text file ([`2573320`](https://github.com/dstroy0/ProtoCore/commit/2573320328b0b5065c89b84c63493073ef6eea95))
- unbreak the default link, and stop the tree walk storing every path ten times ([`2602f75`](https://github.com/dstroy0/ProtoCore/commit/2602f75c7cfc1f34b9957912724a4479fa7ae40e))
- stop protocore.h from defining a secret and declaring six symbols nobody can link ([`e8e2853`](https://github.com/dstroy0/ProtoCore/commit/e8e28535ae1defd11b5139d3b17eb619f102a95e))
- native_ssh satisfies the SFTP/SCP guard through the mount, not FILE_SERVING ([`8915c33`](https://github.com/dstroy0/ProtoCore/commit/8915c330d25e0e8380a8aeabfc89d601744cfdd6))
- reject a wire length that overflows the bounds check on 32-bit targets ([`ae8cad2`](https://github.com/dstroy0/ProtoCore/commit/ae8cad246f551671d7abadce663661b6013e0b41))
- derive forced feature dependencies instead of rewriting the user's flags ([`88e22b3`](https://github.com/dstroy0/ProtoCore/commit/88e22b35d38ded040b53fecc01709db306d4e781))

### CI / Build

- update CHANGELOG.md [skip ci] ([`92692fe`](https://github.com/dstroy0/ProtoCore/commit/92692feeb69f0fb1eae77e48e94ab7f94f65cf2f))
- update CHANGELOG.md [skip ci] ([`35227fe`](https://github.com/dstroy0/ProtoCore/commit/35227fe81e139d38478bdabfd176d0d415685b18))
- update CHANGELOG.md [skip ci] ([`f8ee54e`](https://github.com/dstroy0/ProtoCore/commit/f8ee54e88c0686db636b3f4e39622c62322fc47e))
- update CHANGELOG.md [skip ci] ([`10be3c0`](https://github.com/dstroy0/ProtoCore/commit/10be3c0340eef80f9a8e417435372a7656e20c34))
- update CHANGELOG.md [skip ci] ([`49b3d78`](https://github.com/dstroy0/ProtoCore/commit/49b3d7834c6f6ba1508d112caaf1771579e4e1a4))
- update test report + coverage [skip ci] ([`10dfc51`](https://github.com/dstroy0/ProtoCore/commit/10dfc517fcb3c4caad3f2566fb3c1121c482021a))
- update CHANGELOG.md [skip ci] ([`b30eabf`](https://github.com/dstroy0/ProtoCore/commit/b30eabf2e5a8e008c58b1f9af9c820655df06b74))
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

- Bump version: 0.0.6 → 0.0.7 ([`4a6ac90`](https://github.com/dstroy0/ProtoCore/commit/4a6ac90370aaad684ff0656dacefe860615f667d))
- Bump version: 0.0.5 → 0.0.6 ([`0b8de8e`](https://github.com/dstroy0/ProtoCore/commit/0b8de8e038484f35d0864a0595895ae7c966d014))
- Bump version: 0.0.4 → 0.0.5 ([`df66a22`](https://github.com/dstroy0/ProtoCore/commit/df66a22c04544a452ddb3ba16b8e87f8bd55e649))
- Bump version: 0.0.3 → 0.0.4 ([`4619ab9`](https://github.com/dstroy0/ProtoCore/commit/4619ab9624c47d45d0d5d73ca896c87fffd57e06))
- Merge remote-tracking branch 'origin/main' into refactor/lib-wide ([`dd7a7a1`](https://github.com/dstroy0/ProtoCore/commit/dd7a7a1e354b12feee9b5365653e217b872fd0fe))
- Merge pull request #20 from dstroy0/refactor/json-codec ([`3103251`](https://github.com/dstroy0/ProtoCore/commit/3103251059e3e69ff6dca2ab00a27367a1d38903))
- Merge remote-tracking branch 'origin/main' into refactor/json-codec ([`f12ca3f`](https://github.com/dstroy0/ProtoCore/commit/f12ca3f301fd878bf606ad34c2c9b6800e24ae14))
- Bump version: 0.0.2 → 0.0.3 ([`bb17397`](https://github.com/dstroy0/ProtoCore/commit/bb173979946afe7c85ee83f7bbe27ef84d16f81b))
- Bump version: 0.0.1 → 0.0.2 ([`2c6672b`](https://github.com/dstroy0/ProtoCore/commit/2c6672bdd72389bf9903f822b4b94ed2a07434cd))
- drop the clip mode; logging takes the one contract ([`6195264`](https://github.com/dstroy0/ProtoCore/commit/61952644c3afebec52a40acce1bf4014ec74c3e9))
- delete the duplicate web_assets copy that broke every example link ([`7df281f`](https://github.com/dstroy0/ProtoCore/commit/7df281fc16d7d5903427fcfcbbe5d31bb4e1de04))
- close ban 20 - the three printf APIs take a frame spec ([`6505d73`](https://github.com/dstroy0/ProtoCore/commit/6505d73ed0ce76b23cf10fefca3c8f5eef5a6492))
- build the last fixed-shape frames with pc_sb, not snprintf ([`114a275`](https://github.com/dstroy0/ProtoCore/commit/114a2752802ef2fbbd228412cee88f0026bd004f))
- ban nondeterministic dispatch, retire the last format-string appender ([`210dd35`](https://github.com/dstroy0/ProtoCore/commit/210dd35746cb481abed5713bab282dbad1d24a93))
- pin LF checkout on every platform ([`692024f`](https://github.com/dstroy0/ProtoCore/commit/692024f92ae597f2cfd45906c24dcd714914b4d7))

### Documentation

- regenerate feature tables + configurator + build_opt.h + example index [skip ci] ([`5127742`](https://github.com/dstroy0/ProtoCore/commit/51277427f26c15063c7af53ff87bc185c0ebe7cb))
- regenerate feature tables + configurator + build_opt.h + example index [skip ci] ([`7f88800`](https://github.com/dstroy0/ProtoCore/commit/7f88800b01dfd0308e1487019dac72888363b3d7))
- state C11 in the law, and retire DONE as a status in the sweep notes ([`0a52923`](https://github.com/dstroy0/ProtoCore/commit/0a529237c945b81248cbce7198770475a65790ec))
- regenerate feature tables + configurator + build_opt.h + example index [skip ci] ([`90e44e7`](https://github.com/dstroy0/ProtoCore/commit/90e44e76e787703bf942a4168e5b25b7fd2302f0))
- regenerate feature tables + configurator + build_opt.h + example index [skip ci] ([`ab43d04`](https://github.com/dstroy0/ProtoCore/commit/ab43d047b2a0dd1465890580c10c5d341d0be9a1))
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

### Features

- add a mock vendor so the hot path is testable on a host ([`a53832c`](https://github.com/dstroy0/ProtoCore/commit/a53832c3633703af390f753eec3e1e3a2f35f0a7))
- give the server's state one place to be read from ([`10b5eba`](https://github.com/dstroy0/ProtoCore/commit/10b5ebad902d6687790fb7fb60fe7775f8b7ea0f))

### Refactor

- move the TLS 1.3 key schedule into network_drivers/tls ([`ba1f6f2`](https://github.com/dstroy0/ProtoCore/commit/ba1f6f213d0d9b7417c73e07445da63292c8475f))
- mirror the layer each module moved to ([`f575b3d`](https://github.com/dstroy0/ProtoCore/commit/f575b3dbfe9d40e243dbb67fffd804884cf2b59c))
- move exc_decoder and power_mgmt into server/ ([`d14ca1e`](https://github.com/dstroy0/ProtoCore/commit/d14ca1e69c88b656b593965e9b5cf1cc569dab97))
- move each module under the layer that owns it ([`bddf3f4`](https://github.com/dstroy0/ProtoCore/commit/bddf3f4a37136505974a95c4ae7054d78423c16e))
- convert src/ to C11 and split the build into PROTOCORE_HOT / PROTOCORE_HOST ([`40d217a`](https://github.com/dstroy0/ProtoCore/commit/40d217a7272b721e83230cb1ca6d0517622824e2))
- split swar into an access layer and bounded-run operations ([`d5d947e`](https://github.com/dstroy0/ProtoCore/commit/d5d947ed28b48109556e8bce14e5fbeb55a80f90))
- finish removing the PC class, and default every feature off ([`65a3886`](https://github.com/dstroy0/ProtoCore/commit/65a3886a0bd147bcc5d039fab5243a86db53ce4a))
- give the filesystem accessor the tree operations, and mnt back its blindness ([`09227b6`](https://github.com/dstroy0/ProtoCore/commit/09227b6ca482d960510a2215af1ed390e118d98b))
- delete the PC class and give the vfs/mnt split its boundary back ([`8b089bb`](https://github.com/dstroy0/ProtoCore/commit/8b089bb8a32bc00f7debb6a512f11d90b5766975))
- give storage one owner and take the vendor out of the file-transfer servers ([`5081912`](https://github.com/dstroy0/ProtoCore/commit/5081912b66c3d24aa60b61ca3ed57d462e9f0a36))
- take the codec's tag byte out of the shared read cursor ([`d4956d8`](https://github.com/dstroy0/ProtoCore/commit/d4956d85f4a3382d7ddfccf836bf44c33e32bef7))
- collapse the codec cursors onto pc_span and give SSH signaling an owner ([`e2d0b4e`](https://github.com/dstroy0/ProtoCore/commit/e2d0b4e7a104053a6135ca68dc7955ed59fa9687))

## [0.0.1] - 2026-07-31

<details>
<summary><b>Show Changelog for version 0.0.1 - 2026-07-31</b></summary>

### Changes

- ProtoCore 0.0.1 ([`dfc3436`](https://github.com/dstroy0/ProtoCore/commit/dfc343615028920abe5045f94e57b2012b273675))

</details>
