# changelog

This file contains highlights of what changes on each version of the observe
package.

#### Pub version 0.11.0
  * Updated to match [observe-js#e212e74][e212e74] (release 0.3.4)
  * ListPathObserver has been deprecated  (it was deleted a while ago in
    observe-js). We plan to delete it in a future release. You may copy the code
    if you still need it.
  * PropertyPath now uses an expression syntax including indexers. For example,
    you can write `a.b["m"]` instead of `a.b.m`.
  * **breaking change**: PropertyPath no longer allows numbers as fields, you
    need to use indexers instead. For example, you now need to write `a[3].d`
    instead of `a.3.d`.
  * **breaking change**: PathObserver.value= no longer discards changes (this is
    in combination with a change in template_binding and polymer to improve
    interop with JS custom elements).

#### Pub version 0.10.0+3
  * minor changes to documentation, deprecated `discardListChages` in favor of
    `discardListChanges` (the former had a typo).

#### Pub version 0.10.0
  * package:observe no longer declares @MirrorsUsed. The package uses mirrors
    for development time, but assumes frameworks (like polymer) and apps that
    use it directly will either generate code that replaces the use of mirrors,
    or add the @MirrorsUsed declaration themselves. For convinience, you can
    import 'package:observe/mirrors_used.dart', and that will add a @MirrorsUsed
    annotation that preserves properties and classes labeled with @reflectable
    and properties labeled with @observable.
  * Updated to match [observe-js#0152d54][0152d54]

[0152d54]: https://github.com/Polymer/observe-js/blob/0152d542350239563d0f2cad39d22d3254bd6c2a/src/observe.js
[e212e74]: https://github.com/Polymer/observe-js/blob/e212e7473962067c099a3d1859595c2f8baa36d7/src/observe.js
