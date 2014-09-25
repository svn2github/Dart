#### Pub version 0.7.1
  * Update to platform version 0.4.1-d214582.

#### Pub version 0.7.0+1
  * Cherry pick https://github.com/Polymer/ShadowDOM/pull/506 to fix IOS 8.

#### Pub version 0.7.0
  * Updated to 0.4.0-5a7353d release, with same cherry pick as 0.6.0+1.
  * Many features were moved into the polymer package, this package is now
    purely focused on polyfills.
  * Change Platform.deliverDeclarations to 
    Platform.consumeDeclarations(callback).
  * Cherry pick https://github.com/Polymer/ShadowDOM/pull/505 to fix mem leak.

#### Pub version 0.6.0+1
  * Cherry pick https://github.com/Polymer/ShadowDOM/pull/500 to fix
    http://dartbug.com/20141. Fixes getDefaultComputedStyle in firefox.

#### Pub version 0.6.0
  * Upgrades to platform master as of 8/25/2014 (see lib/build.log for details).
    This is more recent than the 0.3.5 release as there were multiple breakages
    that required updating past that.
  * There is a bug in this version where selecting non-rendered elements doesn't
    work, but it shouldn't affect most people. See 
    https://github.com/Polymer/ShadowDOM/issues/495.

#### Pub version 0.5.0+1
  * Backward compatible change to prepare for upcoming change of the user agent
    in Dartium.

#### Pub version 0.5.0
  * Upgrades to platform version 0.3.4-02a0f66 (see lib/build.log for details).

#### Pub version 0.4.0
  * Adds `registerDartType` and updates to platform 0.3.3-29065bc
    (re-applies the changes in 0.3.5).

#### Pub version 0.3.5+1
  * Reverts back to what we had in 0.3.4. (The platform.js updates in 0.3.5 had
    breaking changes so we are republishing it in 0.4.0)

#### Pub version 0.3.5
  * Added `registerDartType` to register a Dart API for a custom-element written
    in Javascript.
  * Updated to platform 0.3.3-29065bc

#### Pub version 0.3.4
  * Updated to platform 0.2.4 (see lib/build.log for details)
