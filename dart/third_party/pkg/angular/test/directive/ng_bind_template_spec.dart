library ng_bind_template_spec;

import '../_specs.dart';

main() {
  describe('BindTemplateDirective', () {
    TestBed _;

    beforeEach(inject((TestBed tb) => _ = tb));

    it('should bind template',
          inject((Scope scope, Injector injector, Compiler compiler) {
      var element = _.compile('<div ng-bind-template="{{salutation}} {{name}}!"></div>');
      scope.salutation = 'Hello';
      scope.name = 'Heisenberg';
      scope.$digest();

      expect(element.text).toEqual('Hello Heisenberg!');

      scope.salutation = 'Good-Bye';
      scope.$digest();

      expect(element.text).toEqual('Good-Bye Heisenberg!');
    }));
  });
}
