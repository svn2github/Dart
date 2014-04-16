// Copyright (c) 2014, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

library driver;

import 'dart:io';

import 'package:analysis_server/http_server.dart';
import 'package:analysis_server/src/socket_server.dart';
import 'package:analysis_server/stdio_server.dart';
import 'package:args/args.dart';

/**
 * The [Driver] class represents a single running instance of the analysis
 * server application.  It is responsible for parsing command line options
 * and starting the HTTP and/or stdio servers.
 */
class Driver {
  /**
   * The name of the application that is used to start a server.
   */
  static const BINARY_NAME = 'server';

  /**
   * The name of the option used to print usage information.
   */
  static const String HELP_OPTION = "help";

  /**
   * The name of the option used to specify the port to which the server will
   * connect.
   */
  static const String PORT_OPTION = "port";

  SocketServer socketServer = new SocketServer();

  HttpAnalysisServer httpServer;

  StdioAnalysisServer stdioServer;

  Driver() {
    httpServer = new HttpAnalysisServer(socketServer);
    stdioServer = new StdioAnalysisServer(socketServer);
  }

  /**
   * Use the given command-line arguments to start this server.
   */
  void start(List<String> args) {
    ArgParser parser = new ArgParser();
    parser.addFlag(HELP_OPTION, help:
        "print this help message without starting a server", defaultsTo: false,
        negatable: false);
    parser.addOption(PORT_OPTION, help:
        "[port] the port on which the server will listen");

    ArgResults results = parser.parse(args);
    if (results[HELP_OPTION]) {
      _printUsage(parser);
      return;
    }
    if (results[PORT_OPTION] == null) {
      print('Missing required port number');
      print('');
      _printUsage(parser);
      exitCode = 1;
      return;
    }

    try {
      int port = int.parse(results[PORT_OPTION]);
      httpServer.serveHttp(port);
    } on FormatException {
      print('Invalid port number: ${results[PORT_OPTION]}');
      print('');
      _printUsage(parser);
      exitCode = 1;
      return;
    }
    stdioServer.serveStdio().then((_) {
      httpServer.close();
    });
  }

  /**
   * Print information about how to use the server.
   */
  void _printUsage(ArgParser parser) {
    print('Usage: $BINARY_NAME [flags]');
    print('');
    print('Supported flags are:');
    print(parser.getUsage());
  }
}
