/*
 * Copyright (c) 2013, the Dart project authors.
 * 
 * Licensed under the Eclipse Public License v1.0 (the "License"); you may not use this file except
 * in compliance with the License. You may obtain a copy of the License at
 * 
 * http://www.eclipse.org/legal/epl-v10.html
 * 
 * Unless required by applicable law or agreed to in writing, software distributed under the License
 * is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
 * or implied. See the License for the specific language governing permissions and limitations under
 * the License.
 */
package com.google.dart.engine.services.internal.correction;

import com.google.common.base.CharMatcher;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.Lists;
import com.google.common.collect.Sets;
import com.google.dart.engine.ast.ASTNode;
import com.google.dart.engine.ast.AsExpression;
import com.google.dart.engine.ast.BinaryExpression;
import com.google.dart.engine.ast.Block;
import com.google.dart.engine.ast.CompilationUnit;
import com.google.dart.engine.ast.ConstructorDeclaration;
import com.google.dart.engine.ast.Expression;
import com.google.dart.engine.ast.FunctionDeclaration;
import com.google.dart.engine.ast.FunctionExpression;
import com.google.dart.engine.ast.MethodDeclaration;
import com.google.dart.engine.ast.MethodInvocation;
import com.google.dart.engine.ast.NamedExpression;
import com.google.dart.engine.ast.PrefixedIdentifier;
import com.google.dart.engine.ast.SimpleIdentifier;
import com.google.dart.engine.ast.Statement;
import com.google.dart.engine.ast.StringLiteral;
import com.google.dart.engine.ast.visitor.GeneralizingASTVisitor;
import com.google.dart.engine.element.ClassElement;
import com.google.dart.engine.element.Element;
import com.google.dart.engine.element.ElementKind;
import com.google.dart.engine.element.ExecutableElement;
import com.google.dart.engine.element.ParameterElement;
import com.google.dart.engine.element.visitor.GeneralizingElementVisitor;
import com.google.dart.engine.formatter.edit.Edit;
import com.google.dart.engine.scanner.TokenType;
import com.google.dart.engine.services.internal.util.ExecutionUtils;
import com.google.dart.engine.services.internal.util.RunnableObjectEx;
import com.google.dart.engine.services.internal.util.TokenUtils;
import com.google.dart.engine.source.Source;
import com.google.dart.engine.type.InterfaceType;
import com.google.dart.engine.type.Type;
import com.google.dart.engine.utilities.source.SourceRange;

import static com.google.dart.engine.utilities.source.SourceRangeFactory.rangeEndEnd;
import static com.google.dart.engine.utilities.source.SourceRangeFactory.rangeEndStart;
import static com.google.dart.engine.utilities.source.SourceRangeFactory.rangeNode;
import static com.google.dart.engine.utilities.source.SourceRangeFactory.rangeNodes;
import static com.google.dart.engine.utilities.source.SourceRangeFactory.rangeStartEnd;
import static com.google.dart.engine.utilities.source.SourceRangeFactory.rangeStartStart;

import org.apache.commons.lang3.StringUtils;

import java.nio.CharBuffer;
import java.util.Collection;
import java.util.Collections;
import java.util.Comparator;
import java.util.LinkedList;
import java.util.List;
import java.util.Set;

/**
 * Utilities for analyzing {@link CompilationUnit}, its parts and source.
 */
public class CorrectionUtils {
  /**
   * Describes where to insert new directive or top-level declaration at the top of file.
   */
  public class TopInsertDesc {
    public int offset;
    public boolean insertEmptyLineBefore;
    public boolean insertEmptyLineAfter;
  }

  private static final String[] KNOWN_METHOD_NAME_PREFIXES = {"get", "is", "to"};

  /**
   * The default end-of-line marker for the current platform.
   */
  public static final String DEFAULT_END_OF_LINE = System.getProperty("line.separator", "\n");

  /**
   * @return <code>true</code> if given {@link List}s are equals at given position.
   */
  public static <T> boolean allListsEqual(List<List<T>> lists, int position) {
    T element = lists.get(0).get(position);
    for (List<T> list : lists) {
      if (list.get(position) != element) {
        return false;
      }
    }
    return true;
  }

  /**
   * @return the updated {@link String} with applied {@link Edit}s.
   */
  public static String applyReplaceEdits(String s, List<Edit> edits) {
    // sort edits
    edits = Lists.newArrayList(edits);
    Collections.sort(edits, new Comparator<Edit>() {
      @Override
      public int compare(Edit o1, Edit o2) {
        return o1.offset - o2.offset;
      }
    });
    // apply edits
    int delta = 0;
    for (Edit edit : edits) {
      int editOffset = edit.offset + delta;
      String beforeEdit = s.substring(0, editOffset);
      String afterEdit = s.substring(editOffset + edit.length);
      s = beforeEdit + edit.replacement + afterEdit;
      delta += getDeltaOffset(edit);
    }
    // done
    return s;
  }

  /**
   * @return <code>true</code> if given {@link SourceRange} covers given {@link ASTNode}.
   */
  public static boolean covers(SourceRange r, ASTNode node) {
    SourceRange nodeRange = rangeNode(node);
    return r.covers(nodeRange);
  }

  /**
   * @return all direct children of the given {@link Element}.
   */
  public static List<Element> getChildren(final Element parent) {
    return getChildren(parent, null);
  }

  /**
   * @param name the required name of children; may be <code>null</code> to get children with any
   *          name.
   * @return all direct children of the given {@link Element}, with given name.
   */
  public static List<Element> getChildren(final Element parent, final String name) {
    final List<Element> children = Lists.newArrayList();
    parent.accept(new GeneralizingElementVisitor<Void>() {
      @Override
      public Void visitElement(Element element) {
        if (element == parent) {
          super.visitElement(element);
        } else if (name == null || element.getName().equals(name)) {
          children.add(element);
        }
        return null;
      }
    });
    return children;
  }

  /**
   * @return the number of characters this {@link Edit} will move offsets after its range.
   */
  public static int getDeltaOffset(Edit edit) {
    return edit.replacement.length() - edit.length;
  }

  /**
   * @return the name of the {@link Element} kind.
   */
  public static String getElementKindName(Element element) {
    ElementKind kind = element.getKind();
    return kind.getDisplayName();
  }

  /**
   * @return the human name of the {@link Element}.
   */
  public static String getElementQualifiedName(Element element) {
    ElementKind kind = element.getKind();
    switch (kind) {
      case FIELD:
      case METHOD:
        return element.getEnclosingElement().getName() + "." + element.getName();
      default:
        return element.getName();
    }
  }

  /**
   * @return the {@link ExecutableElement} of the enclosing executable {@link ASTNode}.
   */
  public static ExecutableElement getEnclosingExecutableElement(ASTNode node) {
    while (node != null) {
      if (node instanceof FunctionDeclaration) {
        return ((FunctionDeclaration) node).getElement();
      }
      if (node instanceof ConstructorDeclaration) {
        return ((ConstructorDeclaration) node).getElement();
      }
      if (node instanceof MethodDeclaration) {
        return ((MethodDeclaration) node).getElement();
      }
      node = node.getParent();
    }
    return null;
  }

  /**
   * @return the enclosing executable {@link ASTNode}.
   */
  public static ASTNode getEnclosingExecutableNode(ASTNode node) {
    while (node != null) {
      if (node instanceof FunctionDeclaration) {
        return node;
      }
      if (node instanceof ConstructorDeclaration) {
        return node;
      }
      if (node instanceof MethodDeclaration) {
        return node;
      }
      node = node.getParent();
    }
    return null;
  }

  /**
   * @return the nearest common ancestor {@link ASTNode} of the given {@link ASTNode}s.
   */
  public static ASTNode getNearestCommonAncestor(List<ASTNode> nodes) {
    // may be no nodes
    if (nodes.isEmpty()) {
      return null;
    }
    // prepare parents
    List<List<ASTNode>> parents = Lists.newArrayList();
    for (ASTNode node : nodes) {
      parents.add(getParents(node));
    }
    // find min length
    int minLength = Integer.MAX_VALUE;
    for (List<ASTNode> parentList : parents) {
      minLength = Math.min(minLength, parentList.size());
    }
    // find deepest parent
    int i = 0;
    for (; i < minLength; i++) {
      if (!allListsEqual(parents, i)) {
        break;
      }
    }
    return parents.get(0).get(i - 1);
  }

  /**
   * @return parent {@link ASTNode}s from {@link CompilationUnit} (at index "0") to the given one.
   */
  public static List<ASTNode> getParents(ASTNode node) {
    LinkedList<ASTNode> parents = Lists.newLinkedList();
    ASTNode current = node;
    do {
      parents.addFirst(current.getParent());
      current = current.getParent();
    } while (current.getParent() != null);
    return parents;
  }

  /**
   * @return given {@link Statement} if not {@link Block}, first child {@link Statement} if
   *         {@link Block}, or <code>null</code> if more than one child.
   */
  public static Statement getSingleStatement(Statement statement) {
    if (statement instanceof Block) {
      List<Statement> blockStatements = ((Block) statement).getStatements();
      if (blockStatements.size() != 1) {
        return null;
      }
      return blockStatements.get(0);
    }
    return statement;
  }

  /**
   * @return given {@link DartStatement} if not {@link DartBlock}, all children
   *         {@link DartStatement}s if {@link DartBlock}.
   */
  public static List<Statement> getStatements(Statement statement) {
    if (statement instanceof Block) {
      return ((Block) statement).getStatements();
    }
    return ImmutableList.of(statement);
  }

  /**
   * @return the whitespace prefix of the given {@link String}.
   */
  public static String getStringPrefix(String s) {
    int index = CharMatcher.WHITESPACE.negate().indexIn(s);
    if (index == -1) {
      return s;
    }
    return s.substring(0, index);
  }

  /**
   * @return the {@link Set} with all direct and indirect super {@link ClassElement}s of the given.
   */
  public static Set<ClassElement> getSuperClassElements(ClassElement classElement) {
    Set<ClassElement> classes = Sets.newHashSet();
    for (InterfaceType superType : classElement.getAllSupertypes()) {
      ClassElement superClass = superType.getElement();
      classes.add(superClass);
    }
    return classes;
  }

  /**
   * @return the actual type source of the given {@link Expression}, may be <code>null</code> if can
   *         not be resolved, should be treated as <code>Dynamic</code>.
   */
  public static String getTypeSource(Expression expression) {
    if (expression == null) {
      return null;
    }
    Type type = expression.getStaticType();
    String typeSource = getTypeSource(type);
    if ("dynamic".equals(typeSource)) {
      return null;
    }
    return typeSource;
  }

  /**
   * @return the source of the given {@link Type}.
   */
  public static String getTypeSource(Type type) {
    String typeSource = type.toString();
    typeSource = StringUtils.substringBefore(typeSource, "<");
    return typeSource;
  }

  /**
   * @return the possible names for variable with initializer of the given {@link StringLiteral}.
   */
  public static String[] getVariableNameSuggestions(String text, Set<String> excluded) {
    // filter out everything except of letters and white spaces
    {
      CharMatcher matcher = CharMatcher.JAVA_LETTER.or(CharMatcher.WHITESPACE);
      text = matcher.retainFrom(text);
    }
    // make single camel-case text
    {
      String[] words = StringUtils.split(text);
      StringBuilder sb = new StringBuilder();
      for (int i = 0; i < words.length; i++) {
        String word = words[i];
        if (i > 0) {
          word = StringUtils.capitalize(word);
        }
        sb.append(word);
      }
      text = sb.toString();
    }
    // split camel-case into separate suggested names
    Set<String> res = Sets.newLinkedHashSet();
    addAll(excluded, res, getVariableNameSuggestions(text));
    return res.toArray(new String[res.size()]);
  }

  /**
   * @return the possible names for variable with given expected type and expression.
   */
  public static String[] getVariableNameSuggestions(Type expectedType,
      Expression assignedExpression, Set<String> excluded) {
    Set<String> res = Sets.newLinkedHashSet();
    // use expression
    if (assignedExpression != null) {
      String nameFromExpression = getBaseNameFromExpression(assignedExpression);
      if (nameFromExpression != null) {
        addAll(excluded, res, getVariableNameSuggestions(nameFromExpression));
      }

      String nameFromParent = getBaseNameFromLocationInParent(assignedExpression);
      if (nameFromParent != null) {
        addAll(excluded, res, getVariableNameSuggestions(nameFromParent));
      }
    }
    // use type
    if (expectedType != null && !expectedType.isDynamic()) {
      String typeName = expectedType.getName();
      if ("int".equals(typeName)) {
        addSingleCharacterName(excluded, res, 'i');
      } else if ("double".equals(typeName)) {
        addSingleCharacterName(excluded, res, 'd');
      } else {
        addAll(excluded, res, getVariableNameSuggestions(typeName));
      }
      res.remove(typeName);
    }
    // done
    return res.toArray(new String[res.size()]);
  }

  /**
   * Adds "toAdd" items which are not excluded.
   */
  private static void addAll(Set<String> excluded, Set<String> result, Collection<String> toAdd) {
    for (String item : toAdd) {
      // add name based on "item", but not "excluded"
      for (int suffix = 1;; suffix++) {
        // prepare name, just "item" or "item2", "item3", etc
        String name = item;
        if (suffix > 1) {
          name += suffix;
        }
        // add once found not excluded
        if (!excluded.contains(name)) {
          result.add(name);
          break;
        }
      }
    }
  }

  private static void addSingleCharacterName(Set<String> excluded, Set<String> result, char c) {
    while (c < 'z') {
      String name = String.valueOf(c);
      // may be done
      if (!excluded.contains(name)) {
        result.add(name);
        break;
      }
      // next character
      c = (char) (c + 1);
    }
  }

  private static String getBaseNameFromExpression(Expression expression) {
    String name = null;
    // e as Type
    if (expression instanceof AsExpression) {
      AsExpression asExpression = (AsExpression) expression;
      expression = asExpression.getExpression();
    }
    // analyze expressions
    if (expression instanceof SimpleIdentifier) {
      SimpleIdentifier node = (SimpleIdentifier) expression;
      return node.getName();
    } else if (expression instanceof PrefixedIdentifier) {
      PrefixedIdentifier node = (PrefixedIdentifier) expression;
      return node.getIdentifier().getName();
    } else if (expression instanceof MethodInvocation) {
      name = ((MethodInvocation) expression).getMethodName().getName();
    }
    // strip known prefixes
    if (name != null) {
      for (int i = 0; i < KNOWN_METHOD_NAME_PREFIXES.length; i++) {
        String curr = KNOWN_METHOD_NAME_PREFIXES[i];
        if (name.startsWith(curr)) {
          if (name.equals(curr)) {
            return null; // don't suggest 'get' as variable name
          } else if (Character.isUpperCase(name.charAt(curr.length()))) {
            return name.substring(curr.length());
          }
        }
      }
    }
    // done
    return name;
  }

  private static String getBaseNameFromLocationInParent(Expression expression) {
    // value in named expression
    if (expression.getParent() instanceof NamedExpression) {
      NamedExpression namedExpression = (NamedExpression) expression.getParent();
      if (namedExpression.getExpression() == expression) {
        return namedExpression.getName().getLabel().getName();
      }
    }
    // positional argument
    {
      ParameterElement parameter = expression.getParameterElement();
      if (parameter != null) {
        return parameter.getName();
      }
    }
    // unknown
    return null;
  }

  /**
   * @return {@link Expression}s from <code>operands</code> which are completely covered by given
   *         {@link SourceRange}. Range should start and end between given {@link Expression}s.
   */
  private static List<Expression> getOperandsForSourceRange(List<Expression> operands,
      SourceRange range) {
    assert !operands.isEmpty();
    List<Expression> subOperands = Lists.newArrayList();
    // track range enter/exit
    boolean entered = false;
    boolean exited = false;
    // may be range starts before or on first operand
    if (range.getOffset() <= operands.get(0).getOffset()) {
      entered = true;
    }
    // iterate over gaps between operands
    for (int i = 0; i < operands.size() - 1; i++) {
      Expression operand = operands.get(i);
      Expression nextOperand = operands.get(i + 1);
      SourceRange inclusiveGap = rangeEndStart(operand, nextOperand).getMoveEnd(1);
      // add operand, if already entered range
      if (entered) {
        subOperands.add(operand);
        // may be last operand in range
        if (range.endsIn(inclusiveGap)) {
          exited = true;
        }
      } else {
        // may be first operand in range
        if (range.startsIn(inclusiveGap)) {
          entered = true;
        }
      }
    }
    // check if last operand is in range
    Expression lastGroupMember = operands.get(operands.size() - 1);
    if (range.getEnd() == lastGroupMember.getEnd()) {
      subOperands.add(lastGroupMember);
      exited = true;
    }
    // we expect that range covers only given operands
    if (!exited) {
      return Lists.newArrayList();
    }
    // done
    return subOperands;
  }

  /**
   * @return all operands of the given {@link BinaryExpression} and its children with the same
   *         operator.
   */
  private static List<Expression> getOperandsInOrderFor(BinaryExpression groupRoot) {
    final List<Expression> operands = Lists.newArrayList();
    final TokenType groupOperatorType = groupRoot.getOperator().getType();
    groupRoot.accept(new GeneralizingASTVisitor<Void>() {
      @Override
      public Void visitExpression(Expression node) {
        if (node instanceof BinaryExpression
            && ((BinaryExpression) node).getOperator().getType() == groupOperatorType) {
          return super.visitNode(node);
        }
        operands.add(node);
        return null;
      }
    });
    return operands;
  }

  /**
   * @return all variants of names by removing leading words by one.
   */
  private static List<String> getVariableNameSuggestions(String name) {
    List<String> result = Lists.newArrayList();
    String[] parts = name.split("(?<!(^|[A-Z]))(?=[A-Z])|(?<!^)(?=[A-Z][a-z])");
    for (int i = 0; i < parts.length; i++) {
      String suggestion = parts[i].toLowerCase() + StringUtils.join(parts, "", i + 1, parts.length);
      result.add(suggestion);
    }
    return result;
  }

  private final CompilationUnit unit;
  private String buffer;
  private String endOfLine;

  public CorrectionUtils(CompilationUnit unit) throws Exception {
    this.unit = unit;
    unit.getElement().getSource().getContents(new Source.ContentReceiver() {
      @Override
      public void accept(CharBuffer contents) {
        buffer = contents.toString();
      }

      @Override
      public void accept(String contents) {
        buffer = contents;
      }
    });
  }

  /**
   * @return the source of the given {@link SourceRange} with indentation changed from "oldIndent"
   *         to "newIndent", keeping indentation of the lines relative to each other.
   */
  public Edit createIndentEdit(SourceRange range, String oldIndent, String newIndent) {
    String newSource = getIndentSource(range, oldIndent, newIndent);
    return new Edit(range.getOffset(), range.getLength(), newSource);
  }

  /**
   * @return the EOL to use for this {@link CompilationUnit}.
   */
  public String getEndOfLine() {
    if (endOfLine == null) {
      endOfLine = ExecutionUtils.runObjectIgnore(new RunnableObjectEx<String>() {
        @Override
        public String runObject() throws Exception {
          // try to find Windows
          if (buffer.contains("\r\n")) {
            return "\r\n";
          }
          // use default
          return DEFAULT_END_OF_LINE;
        }
      }, DEFAULT_END_OF_LINE);
    }
    return endOfLine;
  }

  /**
   * @return the default indentation with given level.
   */
  public String getIndent(int level) {
    return StringUtils.repeat("  ", level);
  }

  /**
   * @return the source of the given {@link SourceRange} with indentation changed from "oldIndent"
   *         to "newIndent", keeping indentation of the lines relative to each other.
   */
  public String getIndentSource(SourceRange range, String oldIndent, String newIndent) {
    String oldSource = getText(range);
    return getIndentSource(oldSource, oldIndent, newIndent);
  }

  /**
   * @return the source with indentation changed from "oldIndent" to "newIndent", keeping
   *         indentation of the lines relative to each other.
   */
  public String getIndentSource(String source, String oldIndent, String newIndent) {
    StringBuilder sb = new StringBuilder();
    String eol = getEndOfLine();
    String[] lines = StringUtils.splitPreserveAllTokens(source, eol);
    for (int i = 0; i < lines.length; i++) {
      String line = lines[i];
      // last line, stop if empty
      if (i == lines.length - 1 && StringUtils.isEmpty(line)) {
        break;
      }
      // line should have new indent
      line = newIndent + StringUtils.removeStart(line, oldIndent);
      // append line
      sb.append(line);
      sb.append(eol);
    }
    return sb.toString();
  }

  /**
   * Skips whitespace characters and single EOL on the right from the given position. If from
   * statement or method end, then this is in the most cases start of the next line.
   */
  public int getLineContentEnd(int index) {
    int length = buffer.length();
    // skip whitespace characters
    while (index < length) {
      char c = buffer.charAt(index);
      if (!Character.isWhitespace(c) || c == '\r' || c == '\n') {
        break;
      }
      index++;
    }
    // skip single \r
    if (index < length && buffer.charAt(index) == '\r') {
      index++;
    }
    // skip single \n
    if (index < length && buffer.charAt(index) == '\n') {
      index++;
    }
    // done
    return index;
  }

//  /**
//   * @return {@link TopInsertDesc}, description where to insert new directive or top-level
//   *         declaration at the top of file.
//   */
//  public TopInsertDesc getTopInsertDesc() {
//    // skip leading line comments
//    int offset = 0;
//    boolean insertEmptyLineBefore = false;
//    boolean insertEmptyLineAfter = false;
//    String source = getText();
//    // skip hash-bang
//    if (offset < source.length() - 2) {
//      String linePrefix = getText(offset, 2);
//      if (linePrefix.equals("#!")) {
//        insertEmptyLineBefore = true;
//        offset = getLineNext(offset);
//        // skip empty lines to first line comment
//        int emptyOffset = offset;
//        while (emptyOffset < source.length() - 2) {
//          int nextLineOffset = getLineNext(emptyOffset);
//          String line = source.substring(emptyOffset, nextLineOffset);
//          if (line.trim().isEmpty()) {
//            emptyOffset = nextLineOffset;
//            continue;
//          } else if (line.startsWith("//")) {
//            offset = emptyOffset;
//            break;
//          } else {
//            break;
//          }
//        }
//      }
//    }
//    // skip line comments
//    while (offset < source.length() - 2) {
//      String linePrefix = getText(offset, 2);
//      if (linePrefix.equals("//")) {
//        insertEmptyLineBefore = true;
//        offset = getLineNext(offset);
//      } else {
//        break;
//      }
//    }
//    // determine if empty line required
//    int nextLineOffset = getLineNext(offset);
//    String insertLine = source.substring(offset, nextLineOffset);
//    if (!insertLine.trim().isEmpty()) {
//      insertEmptyLineAfter = true;
//    }
//    // fill TopInsertDesc
//    TopInsertDesc desc = new TopInsertDesc();
//    desc.offset = offset;
//    desc.insertEmptyLineBefore = insertEmptyLineBefore;
//    desc.insertEmptyLineAfter = insertEmptyLineAfter;
//    return desc;
//  }

  /**
   * @return the index of the last space or tab on the left from the given one, if from statement or
   *         method start, then this is in most cases start of the line.
   */
  public int getLineContentStart(int index) {
    while (index > 0) {
      char c = buffer.charAt(index - 1);
      if (c != ' ' && c != '\t') {
        break;
      }
      index--;
    }
    return index;
  }

//  /**
//   * @return the offset of the token on the right from given "offset" on the same line or offset of
//   *         the next line.
//   */
//  public int getTokenOrNextLineOffset(int offset) {
//    int nextOffset = getLineContentEnd(offset);
//    String sourceToNext = getText(offset, nextOffset - offset);
//    List<Token> tokens = TokenUtils.getTokens(sourceToNext);
//    if (tokens.isEmpty()) {
//      return nextOffset;
//    }
//    return tokens.get(0).getOffset();
//  }

  /**
   * @return the start index of the next line after the line which contains given index.
   */
  public int getLineNext(int index) {
    int length = buffer.length();
    // skip whitespace characters
    while (index < length) {
      char c = buffer.charAt(index);
      if (c == '\r' || c == '\n') {
        break;
      }
      index++;
    }
    // skip single \r
    if (index < length && buffer.charAt(index) == '\r') {
      index++;
    }
    // skip single \n
    if (index < length && buffer.charAt(index) == '\n') {
      index++;
    }
    // done
    return index;
  }

  /**
   * @return the whitespace prefix of the line which contains given offset.
   */
  public String getLinePrefix(int index) {
    int lineStart = getLineThis(index);
    int length = buffer.length();
    int lineNonWhitespace = lineStart;
    while (lineNonWhitespace < length) {
      char c = buffer.charAt(lineNonWhitespace);
      if (c == '\r' || c == '\n') {
        break;
      }
      if (!Character.isWhitespace(c)) {
        break;
      }
      lineNonWhitespace++;
    }
    return getText(lineStart, lineNonWhitespace - lineStart);
  }

  /**
   * @return the {@link #getLinesRange(SourceRange)} for given {@link DartStatement}s.
   */
  public SourceRange getLinesRange(List<Statement> statements) {
    SourceRange range = rangeNodes(statements);
    return getLinesRange(range);
  }

  /**
   * @return the {@link SourceRange} which starts at the start of the line of "offset" and ends at
   *         the start of the next line after "end" of the given {@link SourceRange}, i.e. basically
   *         complete lines of the source for given {@link SourceRange}.
   */
  public SourceRange getLinesRange(SourceRange range) {
    // start
    int startOffset = range.getOffset();
    int startLineOffset = getLineContentStart(startOffset);
    // end
    int endOffset = range.getEnd();
    int afterEndLineOffset = getLineContentEnd(endOffset);
    // range
    return rangeStartEnd(startLineOffset, afterEndLineOffset);
  }

  /**
   * @return the start index of the line which contains given index.
   */
  public int getLineThis(int index) {
    while (index > 0) {
      char c = buffer.charAt(index - 1);
      if (c == '\r' || c == '\n') {
        break;
      }
      index--;
    }
    return index;
  }

  /**
   * @return the line prefix consisting of spaces and tabs on the left from the given
   *         {@link ASTNode}.
   */
  public String getNodePrefix(ASTNode node) {
    int offset = node.getOffset();
    // function literal is special, it uses offset of enclosing line
    if (node instanceof FunctionExpression) {
      return getLinePrefix(offset);
    }
    // use just prefix directly before node
    return getPrefix(offset);
  }

  /**
   * @return the index of the first non-whitespace character after given index.
   */
  public int getNonWhitespaceForward(int index) {
    int length = buffer.length();
    // skip whitespace characters
    while (index < length) {
      char c = buffer.charAt(index);
      if (!Character.isWhitespace(c)) {
        break;
      }
      index++;
    }
    // done
    return index;
  }

  /**
   * @return the line prefix consisting of spaces and tabs on the left from the given offset.
   */
  public String getPrefix(int endIndex) {
    int startIndex = getLineContentStart(endIndex);
    return buffer.substring(startIndex, endIndex);
  }

  /**
   * @return the full text of unit.
   */
  public String getText() {
    return buffer;
  }

  /**
   * @return the given range of text from unit.
   */
  public String getText(ASTNode node) {
    return getText(node.getOffset(), node.getLength());
  }

  /**
   * @return the given range of text from unit.
   */
  public String getText(int offset, int length) {
    return buffer.substring(offset, offset + length);
  }

  /**
   * @return the given range of text from unit.
   */
  public String getText(SourceRange range) {
    return getText(range.getOffset(), range.getLength());
  }

  /**
   * @return the underlying {@link CompilationUnit}.
   */
  public CompilationUnit getUnit() {
    return unit;
  }

  /**
   * @return <code>true</code> if selection range contains only whitespace.
   */
  public boolean isJustWhitespace(SourceRange range) {
    return getText(range).trim().length() == 0;
  }

  /**
   * @return <code>true</code> if selection range contains only whitespace or comments
   */
  public boolean isJustWhitespaceOrComment(SourceRange range) {
    String trimmedText = getText(range).trim();
    // may be whitespace
    if (trimmedText.isEmpty()) {
      return true;
    }
    // may be comment
    return TokenUtils.getTokens(trimmedText).isEmpty();
  }

  /**
   * @return <code>true</code> if "selection" covers "node" and there are any non-whitespace tokens
   *         between "selection" and "node" start/end.
   */
  public boolean selectionIncludesNonWhitespaceOutsideNode(SourceRange selection, ASTNode node) {
    return selectionIncludesNonWhitespaceOutsideRange(selection, rangeNode(node));
  }

  /**
   * @return <code>true</code> if given range of {@link BinaryExpression} can be extracted.
   */
  public boolean validateBinaryExpressionRange(BinaryExpression binaryExpression, SourceRange range) {
    // only parts of associative expression are safe to extract
    if (!binaryExpression.getOperator().getType().isAssociativeOperator()) {
      return false;
    }
    // prepare selected operands
    List<Expression> operands = getOperandsInOrderFor(binaryExpression);
    List<Expression> subOperands = getOperandsForSourceRange(operands, range);
    // if empty, then something wrong with selection
    if (subOperands.isEmpty()) {
      return false;
    }
    // may be some punctuation included into selection - operators, braces, etc
    if (selectionIncludesNonWhitespaceOutsideOperands(range, subOperands)) {
      return false;
    }
    // OK
    return true;
  }

  private boolean selectionIncludesNonWhitespaceOutsideOperands(SourceRange selection,
      List<Expression> operands) {
    return selectionIncludesNonWhitespaceOutsideRange(selection, rangeNodes(operands));
  }

  /**
   * @return <code>true</code> if "selection" covers "range" and there are any non-whitespace tokens
   *         between "selection" and "range" start/end.
   */
  private boolean selectionIncludesNonWhitespaceOutsideRange(SourceRange selection,
      SourceRange range) {
    // selection should cover range
    if (!selection.covers(range)) {
      return false;
    }
    // non-whitespace between selection start and range start
    if (!isJustWhitespaceOrComment(rangeStartStart(selection, range))) {
      return true;
    }
    // non-whitespace after range
    if (!isJustWhitespaceOrComment(rangeEndEnd(range, selection))) {
      return true;
    }
    // only whitespace in selection around range
    return false;
  }
}
