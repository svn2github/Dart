// Copyright (c) 2012, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

library trydart.projectServer;

import 'dart:io';

import 'dart:convert' show
    HtmlEscape,
    JSON,
    UTF8;

const Map<int, String> FsEventNames = const <int, String>{
  FileSystemEvent.CREATE: 'create',
  FileSystemEvent.DELETE: 'delete',
  FileSystemEvent.MODIFY: 'modify',
  FileSystemEvent.MOVE: 'move',
};

/// Represents a "project" command. These commands are accessed from the URL
/// "/project?name".
class ProjectCommand {
  final String name;

  /// For each query parameter, this map describes rules for validating them.
  final Map<String, String> rules;

  final Function handle;

  const ProjectCommand(this.name, this.rules, this.handle);
}

class Conversation {
  HttpRequest request;
  HttpResponse response;

  static const String PROJECT_PATH = '/project';

  static const String PACKAGES_PATH = '/packages';

  static const String CONTENT_TYPE = HttpHeaders.CONTENT_TYPE;

  static const String GIT_TAG = 'try_dart_backup';

  static const String COMMIT_MESSAGE = """
Automated backup.

It is safe to delete tag '$GIT_TAG' if you don't need the backup.""";

  static Uri documentRoot = Uri.base;

  static Uri projectRoot = Uri.base.resolve('site/try/src/');

  static Uri packageRoot = Uri.base.resolve('sdk/lib/_internal/');

  static const List<ProjectCommand> COMMANDS = const <ProjectCommand>[
      const ProjectCommand('list', const {'list': null}, handleProjectList),
  ];

  static Stream<FileSystemEvent> projectChanges;

  static final Set<WebSocket> sockets = new Set<WebSocket>();

  Conversation(this.request, this.response);

  onClosed(_) {
    if (response.statusCode == HttpStatus.OK) return;
    print('Request for ${request.uri} ${response.statusCode}');
  }

  notFound(path) {
    response.statusCode = HttpStatus.NOT_FOUND;
    response.write(htmlInfo('Not Found',
                            'The file "$path" could not be found.'));
    response.close();
  }

  redirect(String location) {
    response.statusCode = HttpStatus.FOUND;
    response.headers.add(HttpHeaders.LOCATION, location);
    response.close();
  }

  badRequest(String problem) {
    response.statusCode = HttpStatus.BAD_REQUEST;
    response.write(htmlInfo("Bad request",
                            "Bad request '${request.uri}': $problem"));
    response.close();
  }

  internalError(error, stack) {
    print(error);
    if (stack != null) print(stack);
    response.statusCode = HttpStatus.INTERNAL_SERVER_ERROR;
    response.write(htmlInfo("Internal Server Error",
                            "Internal Server Error: $error\n$stack"));
    response.close();
  }

  bool validate(Map<String, String> parameters, Map<String, String> rules) {
    Iterable<String> problems = rules.keys
        .where((name) => !parameters.containsKey(name))
        .map((name) => "Missing parameter: '$name'.");
    if (!problems.isEmpty) {
      badRequest(problems.first);
      return false;
    }
    Set extra = new Set.from(parameters.keys)..removeAll(rules.keys);
    if (extra.isEmpty) return true;
    String extraString = (extra.toList()..sort()).join("', '");
    badRequest("Extra parameters: '$extraString'.");
    return false;
  }

  static Future<List<String>> listProjectFiles() {
    String nativeDir = projectRoot.toFilePath();
    Directory dir = new Directory(nativeDir);
    var future = dir.list(recursive: true, followLinks: false).toList();
    return future.then((List<FileSystemEntity> entries) {
      return entries
          .map((e) => e.path)
          .where((p) => !p.endsWith('~') && p.startsWith(nativeDir))
          .map((p) => p.substring(nativeDir.length))
          .map((p) => new Uri.file(p).path).toList();
    });
  }

  static handleProjectList(Conversation self) {
    listProjectFiles().then((List<String> files) {
      self.response
          ..write(JSON.encode(files))
          ..close();
    });
  }

  handleProjectRequest() {
    Map<String, String> parameters = request.uri.queryParameters;
    for (ProjectCommand command in COMMANDS) {
      if (parameters.containsKey(command.name)) {
        if (validate(parameters, command.rules)) {
          (command.handle)(this);
        }
        return;
      }
    }
    String commands = COMMANDS.map((c) => c.name).join("', '");
    badRequest("Valid commands are: '$commands'");
  }

  handle() {
    response.done
      .then(onClosed)
      .catchError(onError);

    Uri uri = request.uri;
    if (uri.path == PROJECT_PATH) {
      return handleProjectRequest();
    }
    if (uri.path.endsWith('/')) {
      uri = uri.resolve('index.html');
    }
    if (uri.path == '/css/fonts/fontawesome-webfont.woff') {
      uri = uri.resolve('/fontawesome-webfont.woff');
    }
    if (uri.path.contains('..') || uri.path.contains('%')) {
      return notFound(uri.path);
    }
    String path = uri.path;
    Uri root = documentRoot;
    String dartType = 'application/dart';
    if (path.startsWith('/project/packages/')) {
      root = packageRoot;
      path = path.substring('/project/packages'.length);
    } else if (path.startsWith('${PROJECT_PATH}/')) {
      root = projectRoot;
      path = path.substring(PROJECT_PATH.length);
      dartType = 'text/plain';
    } else if (path.startsWith('${PACKAGES_PATH}/')) {
      root = packageRoot;
      path = path.substring(PACKAGES_PATH.length);
    }

    String filePath = root.resolve('.$path').toFilePath();
    switch (request.method) {
      case 'GET':
        return handleGet(filePath, dartType);
      case 'POST':
        return handlePost(filePath);
      default:
        String method = const HtmlEscape().convert(request.method);
        return badRequest("Unsupported method: '$method'");
    }
  }

  void handleGet(String path, String dartType) {
    var f = new File(path);
    f.exists().then((bool exists) {
      if (!exists) return notFound(request.uri);
      if (path.endsWith('.html')) {
        response.headers.set(CONTENT_TYPE, 'text/html');
      } else if (path.endsWith('.dart')) {
        response.headers.set(CONTENT_TYPE, dartType);
      } else if (path.endsWith('.js')) {
        response.headers.set(CONTENT_TYPE, 'application/javascript');
      } else if (path.endsWith('.ico')) {
        response.headers.set(CONTENT_TYPE, 'image/x-icon');
      } else if (path.endsWith('.appcache')) {
        response.headers.set(CONTENT_TYPE, 'text/cache-manifest');
      }
      f.openRead().pipe(response).catchError(onError);
    });
  }

  handlePost(String path) {
    // The data is sent using a dart:html HttpRequest (aka XMLHttpRequest).
    // According to http://xhr.spec.whatwg.org/, strings are always encoded as
    // UTF-8.
    request.transform(UTF8.decoder).join().then((String data) {
      // The rest of this method is synchronous. This guarantees that we don't
      // make conflicting git changes in response to multiple POST requests.
      try {
        backup(path);
      } catch (e, stack) {
        return internalError(e, stack);
      }

      new File(path).writeAsStringSync(data);

      response
          ..statusCode = HttpStatus.OK
          ..close();
    });
  }

  // Back up the file [path] using git.
  static void backup(String path) {
    // Save the git index (aka staging area).
    String savedIndex = git('write-tree');

    String localModifications = null;

    try {

      // Reset the index.
      git('read-tree', ['HEAD']);

      // Save modifications in index.
      git('update-index', ['--add', path]);

      if (!checkGit('diff', ['--cached', '--quiet'])) {
        // If the file is modified, back it up.
        localModifications = git('write-tree');
      }
    } finally {

      // Restore the saved index.
      git('read-tree', [savedIndex]);
    }

    if (localModifications != null) {
      String tag = 'refs/tags/$GIT_TAG';
      var arguments = ['-p', 'HEAD', '-m', COMMIT_MESSAGE, localModifications];

      if (checkGit('rev-parse',  ['-q', '--verify', tag])) {
        // The tag already exists.

        if (checkGit('diff-tree', ['--quiet', localModifications, tag])) {
          // localModifications are identical to the last backup.
          return;
        }

        // Use the tag as a parent.
        arguments = ['-p', tag]..addAll(arguments);
      }

      // Commit the local modifcations.
      String commit = git('commit-tree', arguments);

      // Create or update the tag.
      git('tag', ['-f', GIT_TAG, commit]);
    }
  }

  static String git(String command,
                    [List<String> arguments = const <String> []]) {
    // TODO(ahe): All git commands must use a custom index.  This is set
    // through GIT_INDEX_FILE.  Use 'git rev-parse --git-dir' to find the git
    // directory, then create the custom index file using 'git read-tree HEAD'
    // if it doesn't exist.
    ProcessResult result = run('git', <String>[command]..addAll(arguments));
    if (result.exitCode != 0) {
      throw 'git error: ${result.stdout}\n${result.stderr}';
    }
    return result.stdout.trim();
  }

  static bool checkGit(String command,
                       [List<String> arguments = const <String> []]) {
    return run('git', <String>[command]..addAll(arguments)).exitCode == 0;
  }

  static ProcessResult run(String executable, List<String> arguments) {
    // print('Running $executable ${arguments.join(" ")}');
    return Process.runSync(executable, arguments);
  }

  static onRequest(HttpRequest request) {
    if (WebSocketTransformer.isUpgradeRequest(request)) {
      WebSocketTransformer.upgrade(request).then(handleWebSocket);
    } else {
      new Conversation(request, request.response).handle();
    }
  }

  static handleWebSocket(WebSocket socket) {
    ensureProjectWatcher();
    listProjectFiles().then((List<String> files) {
      socket.add(JSON.encode({'create': files}));
      sockets.add(socket);
      socket.listen(
          null, cancelOnError: true, onDone: () => sockets.remove(socket));
    });
  }

  static ensureProjectWatcher() {
    if (projectChanges != null) return;
    String nativeDir = projectRoot.toFilePath();
    Directory dir = new Directory(nativeDir);
    projectChanges = dir.watch();
    projectChanges.listen((FileSystemEvent event) {
      String type = event.isDirectory ? 'directory' : 'file';
      String eventType = FsEventNames[event.type];
      if (eventType == null) eventType = 'unknown';
      for (WebSocket socket in sockets) {
        socket.add(JSON.encode({eventType: [event.path]}));
      }
    });
  }

  static onError(error) {
    if (error is HttpException) {
      print('Error: ${error.message}');
    } else {
      print('Error: ${error}');
    }
  }

  String htmlInfo(String title, String text) {
    // No script injection, please.
    title = const HtmlEscape().convert(title);
    text = const HtmlEscape().convert(text);
    return """
<!DOCTYPE html>
<html lang='en'>
<head>
<title>$title</title>
</head>
<body>
<h1>$title</h1>
<p style='white-space:pre'>$text</p>
</body>
</html>
""";
  }
}

main(List<String> arguments) {
  if (arguments.length > 0) {
    Conversation.documentRoot = Uri.base.resolve(arguments[0]);
  }
  var host = '127.0.0.1';
  if (arguments.length > 1) {
    host = arguments[1];
  }
  int port = 0;
  if (arguments.length > 2) {
    port = int.parse(arguments[2]);
  }
  if (arguments.length > 3) {
    Conversation.projectRoot = Uri.base.resolve(arguments[3]);
  }
  if (arguments.length > 4) {
    Conversation.packageRoot = Uri.base.resolve(arguments[4]);
  }
  HttpServer.bind(host, port).then((HttpServer server) {
    print('HTTP server started on http://$host:${server.port}/');
    server.listen(Conversation.onRequest, onError: Conversation.onError);
  }).catchError((e) {
    print("HttpServer.bind error: $e");
    exit(1);
  });
}
