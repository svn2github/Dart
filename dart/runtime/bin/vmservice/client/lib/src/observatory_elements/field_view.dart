// Copyright (c) 2013, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

library field_view_element;

import 'package:logging/logging.dart';
import 'package:polymer/polymer.dart';
import 'observatory_element.dart';

@CustomTag('field-view')
class FieldViewElement extends ObservatoryElement {
  @published Map field;
  FieldViewElement.created() : super.created();

  void refresh(var done) {
    var url = app.locationManager.currentIsolateRelativeLink(field['id']);
    app.requestManager.requestMap(url).then((map) {
        field = map;
    }).catchError((e, trace) {
        Logger.root.severe('Error while refreshing field-view: $e\n$trace');
    }).whenComplete(done);
  }
}