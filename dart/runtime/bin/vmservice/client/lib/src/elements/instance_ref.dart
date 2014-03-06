// Copyright (c) 2013, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

library instance_ref_element;

import 'package:logging/logging.dart';
import 'package:polymer/polymer.dart';
import 'service_ref.dart';

@CustomTag('instance-ref')
class InstanceRefElement extends ServiceRefElement {
  InstanceRefElement.created() : super.created();

  String get name {
    if (ref == null) {
      return super.name;
    }
    return ref['preview'];
  }

  String get hoverText {
    if (ref != null) {
      if (ref['type'] == '@Null') {
        if (ref['id'] == 'objects/optimized-out') {
          return 'This object is no longer needed and has been removed by the optimizing compiler.';
        } else if (ref['id'] == 'objects/collected') {
          return 'This object has been reclaimed by the garbage collector.';
        } else if (ref['id'] == 'objects/expired') {
          return 'The handle to this object has expired.  Consider refreshing the page.';
        } else if (ref['id'] == 'objects/not-initialized') {
          return 'This object will be initialized once it is accessed by the program.';
        } else if (ref['id'] == 'objects/being-initialized') {
          return 'This object is currently being initialized.';
        }
      }
    }
    return '';
  }

  // TODO(turnidge): This is here to workaround vm/dart2js differences.
  dynamic expander() {
    return expandEvent;
  }

  void expandEvent(bool expand, var done) {
    print("Calling expandEvent");
    if (expand) {
      isolate.getMap(objectId).then((map) {
          if (map['type'] == 'Null') {
            // The object is no longer available.  For example, the
            // object id may have expired or the object may have been
            // collected by the gc.
            map['type'] = '@Null';
            ref = map;
          } else {
            ref['fields'] = map['fields'];
            ref['elements'] = map['elements'];
            ref['length'] = map['length'];
          }
        ref['fields'] = map['fields'];
        ref['elements'] = map['elements'];
        ref['length'] = map['length'];
      }).catchError((e, trace) {
          Logger.root.severe('Error while expanding instance-ref: $e\n$trace');
      }).whenComplete(done);
    } else {
      ref['fields'] = null;
      ref['elements'] = null;
      done();
    }
  }
}
