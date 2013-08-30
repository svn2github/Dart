library verify_messages;

import "print_to_list.dart";
import "package:unittest/unittest.dart";

verifyResult(ignored) {
  var lineIterator;
  verify(String s) {
    lineIterator.moveNext();
    var value = lineIterator.current;
    expect(value, s);
  }

  var expanded = lines.expand((line) => line.split("\n")).toList();
  lineIterator = expanded.iterator..moveNext();
  verify("Printing messages for en_US");
  verify("This is a message");
  verify("Another message with parameter hello");
  verify("Characters that need escaping, e.g slashes \\ dollars \${ "
      "(curly braces are ok) and xml reserved characters <& and "
      "quotes \" parameters 1, 2, and 3");
  verify("This string extends across multiple lines.");
  verify("1, b, [c, d]");
  verify('"So-called"');
  verify("Cette chaîne est toujours traduit");
  verify("Interpolation is tricky when it ends a sentence like this.");
  verify("This comes from a method");
  verify("This method is not a lambda");
  verify("This comes from a static method");
  verify("This is missing some translations");
  verify("Ancient Greek hangman characters: 𐅆𐅇.");
  verify("Escapable characters here: ");

  verify('Is zero plural?');
  verify('This is singular.');
  verify('This is plural (2).');
  verify('Alice went to her house');
  verify('Bob went to his house');
  verify('cat went to its litter box');
  verify('Alice, Bob sont allés au magasin');
  verify('Alice est allée au magasin');
  verify('Personne n\'est allé au magasin');
  verify('Bob, Bob sont allés au magasin');
  verify('Alice, Alice sont allées au magasin');
  verify('none');
  verify('one');
  verify('m');
  verify('f');
  verify('7 male');
  verify('7 Canadian dollars');
  verify('5 some currency or other.');
  verify('1 Canadian dollar');
  verify('2 Canadian dollars');

  var fr_lines = expanded.skip(1).skipWhile(
      (line) => !line.contains('----')).toList();
  lineIterator = fr_lines.iterator..moveNext();
  verify("Printing messages for fr");
  verify("Il s'agit d'un message");
  verify("Un autre message avec un seul paramètre hello");
  verify(
      "Caractères qui doivent être échapper, par exemple barres \\ "
      "dollars \${ (les accolades sont ok), et xml/html réservés <& et "
      "des citations \" "
      "avec quelques paramètres ainsi 1, 2, et 3");
  verify("Cette message prend plusiers lignes.");
  verify("1, b, [c, d]");
  verify('"Soi-disant"');
  verify("Cette chaîne est toujours traduit");
  verify(
      "L'interpolation est délicate quand elle se termine une "
          "phrase comme this.");
  verify("Cela vient d'une méthode");
  verify("Cette méthode n'est pas un lambda");
  verify("Cela vient d'une méthode statique");
  verify("Ce manque certaines traductions");
  verify("Anciens caractères grecs jeux du pendu: 𐅆𐅇.");
  verify("Escapes: ");
  verify("\r\f\b\t\v.");

  verify('Est-ce que nulle est pluriel?');
  verify('C\'est singulier');
  verify('C\'est pluriel (2).');
  verify('Alice est allée à sa house');
  verify('Bob est allé à sa house');
  verify('cat est allé à sa litter box');
  verify('Alice, Bob étaient allés à la magasin');
  verify('Alice était allée à la magasin');
  verify('Personne n\'avait allé à la magasin');
  verify('Bob, Bob étaient allés à la magasin');
  verify('Alice, Alice étaient allées à la magasin');
  verify('rien');
  verify('un');
  verify('homme');
  verify('femme');
  verify('7 homme');
  verify('7 dollars Canadiens');
  verify('5 certaine devise ou autre.');
  verify('1 dollar Canadien');
  verify('2 dollars Canadiens');

  var de_lines = fr_lines.skip(1).skipWhile(
      (line) => !line.contains('----')).toList();
  lineIterator = de_lines.iterator..moveNext();
  verify("Printing messages for de_DE");
  verify("Dies ist eine Nachricht");
  verify("Eine weitere Meldung mit dem Parameter hello");
  verify(
      "Zeichen, die Flucht benötigen, zB Schrägstriche \\ Dollar "
      "\${ (geschweiften Klammern sind ok) und xml reservierte Zeichen <& und "
      "Zitate \" Parameter 1, 2 und 3");
  verify("Dieser String erstreckt sich über mehrere "
      "Zeilen erstrecken.");
  verify("1, b, [c, d]");
  verify('"Sogenannt"');
  // This is correct, the message is forced to French, even in a German locale.
  verify("Cette chaîne est toujours traduit");
  verify(
      "Interpolation ist schwierig, wenn es einen Satz wie dieser endet this.");
  verify("Dies ergibt sich aus einer Methode");
  verify("Diese Methode ist nicht eine Lambda");
  verify("Dies ergibt sich aus einer statischen Methode");
  verify("This is missing some translations");
  verify("Antike griechische Galgenmännchen Zeichen: 𐅆𐅇");
  verify("Escapes: ");
  verify("\r\f\b\t\v.");

  verify('Ist Null Plural?');
  verify('Dies ist einmalig');
  verify('Dies ist Plural (2).');
  verify('Alice ging zu ihrem house');
  verify('Bob ging zu seinem house');
  verify('cat ging zu seinem litter box');
  verify('Alice, Bob gingen zum magasin');
  verify('Alice ging in dem magasin');
  verify('Niemand ging zu magasin');
  verify('Bob, Bob gingen zum magasin');
  verify('Alice, Alice gingen zum magasin');
  verify('Null');
  verify('ein');
  verify('Mann');
  verify('Frau');
  verify('7 Mann');
  verify('7 Kanadischen dollar');
  verify('5 einige Währung oder anderen.');
  verify('1 Kanadischer dollar');
  verify('2 Kanadischen dollar');
}