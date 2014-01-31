import 'package:di/di.dart';
import 'package:angular/angular.dart';
import 'package:angular/playback/playback_http.dart';
import 'todo.dart';

@MirrorsUsed(targets: const[
  'angular',
  'angular.core',
  'angular.core.dom',
  'angular.filter',
  'angular.perf',
  'angular.directive',
  'angular.routing',
  'angular.core.parser.dynamic_parser',
  'angular.core.parser.lexer',
  'todo',
  'perf_api',
  'List',
  'NodeTreeSanitizer',
  'PlaybackHttpBackendConfig'
  ],
  override: '*')
import 'dart:mirrors';

import 'dart:html';

main() {

  print(window.location.search);
  var module = new Module()
    ..type(TodoController)
    ..type(PlaybackHttpBackendConfig);

  // If these is a query in the URL, use the server-backed
  // TodoController.  Otherwise, use the stored-data controller.
  var query = window.location.search;
  if (query.contains('?')) {
    module.type(ServerController);
  } else {
    module.type(ServerController, implementedBy: NoServerController);
  }

  if (query == '?record') {
    print('Using recording HttpBackend');
    var wrapper = new HttpBackendWrapper(new HttpBackend());
    module.value(HttpBackendWrapper, new HttpBackendWrapper(new HttpBackend()));
    module.type(HttpBackend, implementedBy: RecordingHttpBackend);
  }

  if (query == '?playback') {
    print('Using playback HttpBackend');
    module.type(HttpBackend, implementedBy: PlaybackHttpBackend);
  }

  ngBootstrap(module:module);
}
