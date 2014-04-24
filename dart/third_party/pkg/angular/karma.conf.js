module.exports = function(config) {
  config.set({
    //logLevel: config.LOG_DEBUG,
    basePath: '.',
    frameworks: ['dart-unittest'],

    // list of files / patterns to load in the browser
    // all tests must be 'included', but all other libraries must be 'served' and
    // optionally 'watched' only.
    files: [
      'test/jasmine_syntax.dart',
      'test/*.dart',
      'test/**/*_spec.dart',
      'test/config/filter_tests.dart',
      {pattern: '**/*.dart', watched: true, included: false, served: true},
      'packages/browser/dart.js',
      'packages/browser/interop.js',
      'packages/shadow_dom/shadow_dom.debug.js'
    ],

    exclude: [
      'test/io/**',
      'test/tools/transformer/**',
      'test/tools/symbol_inspector/**'
    ],

    autoWatch: false,

    // If browser does not capture in given timeout [ms], kill it
    captureTimeout: 20000,
    // 5 minutes is enough time for dart2js to run on Travis...
    browserNoActivityTimeout: 300000,

    plugins: [
      'karma-dart',
      'karma-chrome-launcher',
      'karma-firefox-launcher',
      'karma-script-launcher',
      'karma-junit-reporter',
      '../../../karma-parser-getter-setter'
    ],

    customLaunchers: {
      ChromeNoSandbox: { base: 'Chrome', flags: ['--no-sandbox'] }
    },

    browsers: ['Dartium'],

    preprocessors: {
      'test/core/parser/generated_getter_setter.dart': ['parser-getter-setter']
    },

    junitReporter: {
      outputFile: 'test_out/unit.xml',
      suite: 'unit'
    }
  });
};
