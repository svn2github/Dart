library DocumentTest;
import '../../pkg/unittest/lib/unittest.dart';
import '../../pkg/unittest/lib/html_individual_config.dart';
import 'dart:html';

main() {
  useHtmlIndividualConfiguration();

  var isElement = predicate((x) => x is Element, 'is an Element');
  var isDivElement = predicate((x) => x is DivElement, 'is a DivElement');
  var isAnchorElement =
      predicate((x) => x is AnchorElement, 'is an AnchorElement');
  var isUnknownElement =
      predicate((x) => x is UnknownElement, 'is UnknownElement');

  test('CreateElement', () {
    // FIXME: nifty way crashes, do it boring way.
    expect(new Element.tag('span'), isElement);
    expect(new Element.tag('div'), isDivElement);
    expect(new Element.tag('a'), isAnchorElement);
    expect(new Element.tag('bad_name'), isUnknownElement);
  });

  group('getCssCanvasContext', () {
    test('getCssCanvasContext 2d', () {
      var context = document.getCssCanvasContext('2d', 'testContext', 10, 20);
      expect(context is CanvasRenderingContext2D, true);
      expect(context.canvas.width, 10);
      expect(context.canvas.height, 20);
    });
  });
}
