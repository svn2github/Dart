// Copyright (c) 2012, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

part of html;

class _Utils {
  static double dateTimeToDouble(DateTime dateTime) =>
      dateTime.millisecondsSinceEpoch.toDouble();
  static DateTime doubleToDateTime(double dateTime) {
    try {
      return new DateTime.fromMillisecondsSinceEpoch(dateTime.toInt());
    } catch(_) {
      // TODO(antonnm): treat exceptions properly in bindings and
      // find out how to treat NaNs.
      return null;
    }
  }

  static List convertToList(List list) {
    // FIXME: [possible optimization]: do not copy the array if Dart_IsArray is fine w/ it.
    final length = list.length;
    List result = new List(length);
    result.setRange(0, length, list);
    return result;
  }

  static List convertMapToList(Map map) {
    List result = [];
    map.forEach((k, v) => result.addAll([k, v]));
    return result;
  }

  static int convertCanvasElementGetContextMap(Map map) {
    int result = 0;
    if (map['alpha'] == true) result |= 0x01;
    if (map['depth'] == true) result |= 0x02;
    if (map['stencil'] == true) result |= 0x4;
    if (map['antialias'] == true) result |= 0x08;
    if (map['premultipliedAlpha'] == true) result |= 0x10;
    if (map['preserveDrawingBuffer'] == true) result |= 0x20;

    return result;
  }

  static void populateMap(Map result, List list) {
    for (int i = 0; i < list.length; i += 2) {
      result[list[i]] = list[i + 1];
    }
  }

  static bool isMap(obj) => obj is Map;

  static Map createMap() => {};

  static makeUnimplementedError(String fileName, int lineNo) {
    return new UnsupportedError('[info: $fileName:$lineNo]');
  }

  static window() native "Utils_window";
  static forwardingPrint(String message) native "Utils_forwardingPrint";
  static void spawnDomFunction(Function f, int replyTo) native "Utils_spawnDomFunction";
  static void spawnDomUri(String uri, int replyTo) native "Utils_spawnDomUri";
  static int _getNewIsolateId() native "Utils_getNewIsolateId";
  static bool shadowRootSupported(Document document) native "Utils_shadowRootSupported";
}

class _NPObject extends NativeFieldWrapperClass1 {
  _NPObject.internal();
  static _NPObject retrieve(String key) native "NPObject_retrieve";
  property(String propertyName) native "NPObject_property";
  invoke(String methodName, [List args = null]) native "NPObject_invoke";
}

class _DOMWindowCrossFrame extends NativeFieldWrapperClass1 implements
    WindowBase {
  _DOMWindowCrossFrame.internal();

  // Fields.
  HistoryBase get history native "DOMWindow_history_cross_frame_Getter";
  LocationBase get location native "DOMWindow_location_cross_frame_Getter";
  bool get closed native "DOMWindow_closed_Getter";
  int get length native "DOMWindow_length_Getter";
  WindowBase get opener native "DOMWindow_opener_Getter";
  WindowBase get parent native "DOMWindow_parent_Getter";
  WindowBase get top native "DOMWindow_top_Getter";

  // Methods.
  void close() native "DOMWindow_close_Callback";
  void postMessage(/*SerializedScriptValue*/ message, String targetOrigin, [List messagePorts]) native "DOMWindow_postMessage_Callback";

  // Implementation support.
  String get typeName => "DOMWindow";
}

class _HistoryCrossFrame extends NativeFieldWrapperClass1 implements HistoryBase {
  _HistoryCrossFrame.internal();

  // Methods.
  void back() native "History_back_Callback";
  void forward() native "History_forward_Callback";
  void go(int distance) native "History_go_Callback";

  // Implementation support.
  String get typeName => "History";
}

class _LocationCrossFrame extends NativeFieldWrapperClass1 implements LocationBase {
  _LocationCrossFrame.internal();

  // Fields.
  void set href(String) native "Location_href_Setter";

  // Implementation support.
  String get typeName => "Location";
}

class _DOMStringMap extends NativeFieldWrapperClass1 implements Map<String, String> {
  _DOMStringMap.internal();

  bool containsValue(String value) => Maps.containsValue(this, value);
  bool containsKey(String key) native "DOMStringMap_containsKey_Callback";
  String operator [](String key) native "DOMStringMap_item_Callback";
  void operator []=(String key, String value) native "DOMStringMap_setItem_Callback";
  String putIfAbsent(String key, String ifAbsent()) => Maps.putIfAbsent(this, key, ifAbsent);
  String remove(String key) native "DOMStringMap_remove_Callback";
  void clear() => Maps.clear(this);
  void forEach(void f(String key, String value)) => Maps.forEach(this, f);
  Collection<String> get keys native "DOMStringMap_getKeys_Callback";
  Collection<String> get values => Maps.getValues(this);
  int get length => Maps.length(this);
  bool get isEmpty => Maps.isEmpty(this);
}

final Future<SendPort> __HELPER_ISOLATE_PORT =
    spawnDomFunction(_helperIsolateMain);

// Tricky part.
// Once __HELPER_ISOLATE_PORT gets resolved, it will still delay in .then
// and to delay Timer.run is used. However, Timer.run will try to register
// another Timer and here we got stuck: event cannot be posted as then
// callback is not executed because it's delayed with timer.
// Therefore once future is resolved, it's unsafe to call .then on it
// in Timer code.
SendPort __SEND_PORT;

_sendToHelperIsolate(msg, SendPort replyTo) {
  if (__SEND_PORT != null) {
    __SEND_PORT.send(msg, replyTo);
  } else {
    __HELPER_ISOLATE_PORT.then((port) {
      __SEND_PORT = port;
      __SEND_PORT.send(msg, replyTo);
    });
  }
}

final _TIMER_REGISTRY = new Map<SendPort, Timer>();

const _NEW_TIMER = 'NEW_TIMER';
const _CANCEL_TIMER = 'CANCEL_TIMER';
const _TIMER_PING = 'TIMER_PING';
const _PRINT = 'PRINT';

_helperIsolateMain() {
  port.receive((msg, replyTo) {
    final cmd = msg[0];
    if (cmd == _NEW_TIMER) {
      final duration = new Duration(milliseconds: msg[1]);
      bool periodic = msg[2];
      ping() { replyTo.send(_TIMER_PING); };
      _TIMER_REGISTRY[replyTo] = periodic ?
          new Timer.periodic(duration, (_) { ping(); }) :
          new Timer(duration, ping);
    } else if (cmd == _CANCEL_TIMER) {
      _TIMER_REGISTRY.remove(replyTo).cancel();
    } else if (cmd == _PRINT) {
      final message = msg[1];
      // TODO(antonm): we need somehow identify those isolates.
      print('[From isolate] $message');
    }
  });
}

final _printClosure = window.console.log;
final _pureIsolatePrintClosure = (s) {
  _sendToHelperIsolate([_PRINT, s]);
};

final _forwardingPrintClosure = _Utils.forwardingPrint;
