import 'dart:convert';
import 'package:scheduled_test/scheduled_test.dart';
import 'package:scheduled_test/scheduled_server.dart';
import 'package:shelf/shelf.dart' as shelf;
import '../../lib/src/exit_codes.dart' as exit_codes;
import '../descriptor.dart' as d;
import '../test_pub.dart';
import 'utils.dart';
main() {
  initConfig();
  setUp(d.validPackage.create);
  integration('archives and uploads a package', () {
    var server = new ScheduledServer();
    d.credentialsFile(server, 'access token').create();
    var pub = startPublish(server);
    confirmPublish(pub);
    handleUploadForm(server);
    handleUpload(server);
    server.handle('GET', '/create', (request) {
      return new shelf.Response.ok(JSON.encode({
        'success': {
          'message': 'Package test_pkg 1.0.0 uploaded!'
        }
      }));
    });
    pub.stdout.expect(startsWith('Uploading...'));
    pub.stdout.expect('Package test_pkg 1.0.0 uploaded!');
    pub.shouldExit(exit_codes.SUCCESS);
  });
}
