library pub_tests;
import '../descriptor.dart' as d;
import '../test_pub.dart';
import 'utils.dart';
main() {
  initConfig();
  integration("serves index.html for directories", () {
    d.dir(
        appPath,
        [
            d.appPubspec(),
            d.dir(
                "web",
                [
                    d.file("index.html", "<body>super"),
                    d.dir("sub", [d.file("index.html", "<body>sub")])])]).create();
    pubServe();
    requestShouldSucceed("", "<body>super");
    requestShouldSucceed("sub/", "<body>sub");
    requestShouldRedirect("sub", "/sub/");
    endPubServe();
  });
}
