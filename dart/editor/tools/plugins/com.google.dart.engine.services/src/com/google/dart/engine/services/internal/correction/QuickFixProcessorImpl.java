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

import com.google.common.annotations.VisibleForTesting;
import com.google.common.base.Objects;
import com.google.common.collect.Lists;
import com.google.common.collect.Maps;
import com.google.common.collect.Sets;
import com.google.dart.engine.ast.ASTNode;
import com.google.dart.engine.ast.ClassDeclaration;
import com.google.dart.engine.ast.ClassMember;
import com.google.dart.engine.ast.CompilationUnit;
import com.google.dart.engine.ast.CompilationUnitMember;
import com.google.dart.engine.ast.ConstructorDeclaration;
import com.google.dart.engine.ast.ConstructorInitializer;
import com.google.dart.engine.ast.Directive;
import com.google.dart.engine.ast.Expression;
import com.google.dart.engine.ast.FieldDeclaration;
import com.google.dart.engine.ast.FunctionBody;
import com.google.dart.engine.ast.Identifier;
import com.google.dart.engine.ast.ImportDirective;
import com.google.dart.engine.ast.LibraryDirective;
import com.google.dart.engine.ast.MethodDeclaration;
import com.google.dart.engine.ast.MethodInvocation;
import com.google.dart.engine.ast.PartDirective;
import com.google.dart.engine.ast.SimpleIdentifier;
import com.google.dart.engine.ast.SimpleStringLiteral;
import com.google.dart.engine.ast.TypeName;
import com.google.dart.engine.ast.VariableDeclaration;
import com.google.dart.engine.context.AnalysisContext;
import com.google.dart.engine.element.ClassElement;
import com.google.dart.engine.element.CompilationUnitElement;
import com.google.dart.engine.element.ConstructorElement;
import com.google.dart.engine.element.Element;
import com.google.dart.engine.element.ElementKind;
import com.google.dart.engine.element.ExecutableElement;
import com.google.dart.engine.element.FunctionElement;
import com.google.dart.engine.element.ImportElement;
import com.google.dart.engine.element.LibraryElement;
import com.google.dart.engine.element.MethodElement;
import com.google.dart.engine.element.ParameterElement;
import com.google.dart.engine.element.PrefixElement;
import com.google.dart.engine.element.VariableElement;
import com.google.dart.engine.element.visitor.RecursiveElementVisitor;
import com.google.dart.engine.error.AnalysisError;
import com.google.dart.engine.error.AnalysisErrorWithProperties;
import com.google.dart.engine.error.CompileTimeErrorCode;
import com.google.dart.engine.error.ErrorCode;
import com.google.dart.engine.error.ErrorProperty;
import com.google.dart.engine.error.StaticTypeWarningCode;
import com.google.dart.engine.error.StaticWarningCode;
import com.google.dart.engine.formatter.edit.Edit;
import com.google.dart.engine.parser.ParserErrorCode;
import com.google.dart.engine.sdk.DartSdk;
import com.google.dart.engine.sdk.SdkLibrary;
import com.google.dart.engine.services.assist.AssistContext;
import com.google.dart.engine.services.change.SourceChange;
import com.google.dart.engine.services.correction.CorrectionImage;
import com.google.dart.engine.services.correction.CorrectionKind;
import com.google.dart.engine.services.correction.CorrectionProposal;
import com.google.dart.engine.services.correction.CreateFileCorrectionProposal;
import com.google.dart.engine.services.correction.LinkedPositionProposal;
import com.google.dart.engine.services.correction.QuickFixProcessor;
import com.google.dart.engine.services.correction.SourceCorrectionProposal;
import com.google.dart.engine.services.internal.correction.CorrectionUtils.InsertDesc;
import com.google.dart.engine.services.util.HierarchyUtils;
import com.google.dart.engine.source.FileBasedSource;
import com.google.dart.engine.source.Source;
import com.google.dart.engine.source.SourceFactory;
import com.google.dart.engine.type.InterfaceType;
import com.google.dart.engine.type.Type;
import com.google.dart.engine.utilities.dart.ParameterKind;
import com.google.dart.engine.utilities.instrumentation.Instrumentation;
import com.google.dart.engine.utilities.instrumentation.InstrumentationBuilder;
import com.google.dart.engine.utilities.source.SourceRange;

import static com.google.dart.engine.utilities.source.SourceRangeFactory.rangeEndEnd;
import static com.google.dart.engine.utilities.source.SourceRangeFactory.rangeEndStart;
import static com.google.dart.engine.utilities.source.SourceRangeFactory.rangeError;
import static com.google.dart.engine.utilities.source.SourceRangeFactory.rangeNode;
import static com.google.dart.engine.utilities.source.SourceRangeFactory.rangeStartLength;

import org.apache.commons.lang3.ObjectUtils;
import org.apache.commons.lang3.StringUtils;

import java.io.File;
import java.net.URI;
import java.util.Arrays;
import java.util.Comparator;
import java.util.List;
import java.util.Map;
import java.util.Map.Entry;
import java.util.Set;

/**
 * Implementation of {@link QuickFixProcessor}.
 */
public class QuickFixProcessorImpl implements QuickFixProcessor {
  /**
   * Helper for finding {@link Element} with name closest to the given.
   */
  private static class ClosestElementFinder {
    private final Class<?> targetClass;
    private final String targetName;
    Element element = null;
    int distance = Integer.MAX_VALUE;

    public ClosestElementFinder(Class<?> targetClass, String targetName) {
      this.targetClass = targetClass;
      this.targetName = targetName;
    }

    void update(Element member) {
      if (targetClass.isInstance(member)) {
        int memberDistance = StringUtils.getLevenshteinDistance(member.getName(), targetName);
        if (memberDistance < distance) {
          element = member;
          distance = memberDistance;
        }
      }
    }

    void update(Iterable<? extends Element> members) {
      for (Element member : members) {
        update(member);
      }
    }
  }

  private static final CorrectionProposal[] NO_PROPOSALS = {};

  /**
   * @return the Java {@link File} which corresponds to the given {@link Source}, may be
   *         {@code null} if cannot be determined.
   */
  @VisibleForTesting
  public static File getSourceFile(Source source) {
    if (source instanceof FileBasedSource) {
      FileBasedSource fileBasedSource = (FileBasedSource) source;
      return new File(fileBasedSource.getFullName()).getAbsoluteFile();
    }
    return null;
  }

  private static void addSuperTypeProposals(SourceBuilder sb, Set<Type> alreadyAdded, Type type) {
    if (type != null && !alreadyAdded.contains(type) && type.getElement() instanceof ClassElement) {
      alreadyAdded.add(type);
      ClassElement element = (ClassElement) type.getElement();
      sb.addProposal(CorrectionImage.IMG_CORRECTION_CLASS, element.getName());
      addSuperTypeProposals(sb, alreadyAdded, element.getSupertype());
      for (InterfaceType interfaceType : element.getInterfaces()) {
        addSuperTypeProposals(sb, alreadyAdded, interfaceType);
      }
    }
  }

  /**
   * @return the {@link Edit} to remove {@link SourceRange}.
   */
  private static Edit createRemoveEdit(SourceRange range) {
    return createReplaceEdit(range, "");
  }

  /**
   * @return the {@link Edit} to replace {@link SourceRange} with "text".
   */
  private static Edit createReplaceEdit(SourceRange range, String text) {
    return new Edit(range.getOffset(), range.getLength(), text);
  }

  /**
   * Attempts to convert the given absolute {@link File} to the "package" {@link URI}.
   * 
   * @param context the {@link AnalysisContext} to work in.
   * @param file the absolute {@link File}, not null.
   * @return the "package" {@link URI}, may be {@code null}.
   */
  private static URI findPackageUri(AnalysisContext context, File file) {
    Source fileSource = new FileBasedSource(null, file);
    return context.getSourceFactory().restoreUri(fileSource);
  }

  /**
   * @return the suggestions for given {@link Type} and {@link DartExpression}, not empty.
   */
  private static String[] getArgumentNameSuggestions(Set<String> excluded, Type type,
      Expression expression, int index) {
    String[] suggestions = CorrectionUtils.getVariableNameSuggestions(type, expression, excluded);
    if (suggestions.length != 0) {
      return suggestions;
    }
    return new String[] {"arg" + index};
  }

  /**
   * @return <code>true</code> if given {@link DartNode} could be type name.
   */
  private static boolean mayBeTypeIdentifier(ASTNode node) {
    if (node instanceof SimpleIdentifier) {
      if (node.getParent() instanceof TypeName) {
        return true;
      }
      if (node.getParent() instanceof MethodInvocation) {
        MethodInvocation invocation = (MethodInvocation) node.getParent();
        return invocation.getRealTarget() == node;
      }
    }
    return false;
  }

  /**
   * @return the most specific {@link Type} of the given Expression.
   */
  private static Type typeOf(Expression expr) {
    Type type = expr.getPropagatedType();
    if (type == null) {
      type = expr.getStaticType();
    }
    return type;
  }

  private final List<CorrectionProposal> proposals = Lists.newArrayList();
  private final List<Edit> textEdits = Lists.newArrayList();
  private AnalysisError problem;
  private Source source;
  private CompilationUnit unit;
  private LibraryElement unitLibraryElement;
  private File unitLibraryFile;
  private File unitLibraryFolder;

//  private SourceRange proposalEndRange = null;

  private ASTNode node;

  private int selectionOffset;

  private int selectionLength;

  private CorrectionUtils utils;

  private final Map<SourceRange, Edit> positionStopEdits = Maps.newHashMap();

  // TODO(scheglov) implement this
//  private void addFix_createConstructor() {
//    DartNewExpression newExpression = null;
//    DartNode nameNode = null;
//    String namePrefix = null;
//    String name = null;
//    // prepare "new X()"
//    if (node instanceof DartIdentifier && node.getParent().getParent() instanceof DartNewExpression) {
//      newExpression = (DartNewExpression) node.getParent().getParent();
//      // default constructor
//      if (node.getParent() instanceof DartTypeNode) {
//        namePrefix = ((DartIdentifier) node).getName();
//        name = "";
//      }
//      // named constructor
//      if (node.getParent() instanceof DartPropertyAccess) {
//        DartPropertyAccess constructorNameNode = (DartPropertyAccess) node.getParent();
//        nameNode = constructorNameNode.getName();
//        namePrefix = constructorNameNode.getQualifier().toSource() + ".";
//        name = constructorNameNode.getName().getName();
//      }
//    }
//    // prepare environment
//    String eol = utils.getEndOfLine();
//    String prefix = "  ";
//    CompilationUnit targetUnit;
//    SourceRange range;
//    {
//      ClassElement targetElement = (ClassElement) newExpression.getType().getElement();
//      {
//        SourceInfo targetSourceInfo = targetElement.getSourceInfo();
//        Source targetSource = targetSourceInfo.getSource();
//        IResource targetResource = ResourceUtil.getResource(targetSource);
//        targetUnit = (CompilationUnit) DartCore.create(targetResource);
//      }
//      range = SourceRangeFactory.forStartLength(
//          targetElement.getOpenBraceOffset() + "{".length(),
//          0);
//    }
//    // build source
//    SourceBuilder sb = new SourceBuilder(range);
//    {
//      sb.append(eol);
//      sb.append(prefix);
//      // append name
//      {
//        sb.append(namePrefix);
//        if (name != null) {
//          sb.startPosition("NAME");
//          sb.append(name);
//          sb.endPosition();
//        }
//      }
//      addFix_unresolvedMethodCreate_parameters(sb, newExpression);
//      sb.append(") {" + eol + prefix + "}");
//      sb.append(eol);
//    }
//    // insert source
//    addReplaceEdit(range, sb.toString());
//    // add linked positions
//    // TODO(scheglov) disabled, caused exception in old model, don't know why
////    if (Objects.equal(targetUnit, unit) && nameNode != null) {
////      addLinkedPosition("NAME", TrackedPositions.forNode(nameNode));
////    }
//    addLinkedPositions(sb);
//    // add proposal
//    {
//      String msg = Messages.format(
//          CorrectionMessages.QuickFixProcessor_createConstructor,
//          namePrefix + name);
//      addUnitCorrectionProposal(targetUnit, TextFileChange.FORCE_SAVE, msg, OBJ_CONSTRUCTOR_IMG);
//    }
//  }

  private final Map<String, List<SourceRange>> linkedPositions = Maps.newHashMap();

  private final Map<String, List<LinkedPositionProposal>> linkedPositionProposals = Maps.newHashMap();

  @Override
  public CorrectionProposal[] computeProposals(AssistContext context, AnalysisError problem)
      throws Exception {
    if (context == null) {
      return NO_PROPOSALS;
    }
    if (problem == null) {
      return NO_PROPOSALS;
    }
    this.problem = problem;
    proposals.clear();
    selectionOffset = problem.getOffset();
    selectionLength = problem.getLength();
    source = context.getSource();
    unit = context.getCompilationUnit();
    // prepare elements
    {
      CompilationUnitElement unitElement = unit.getElement();
      if (unitElement == null) {
        return NO_PROPOSALS;
      }
      unitLibraryElement = unitElement.getLibrary();
      if (unitLibraryElement == null) {
        return NO_PROPOSALS;
      }
      unitLibraryFile = getSourceFile(unitLibraryElement.getSource());
      if (unitLibraryFile == null) {
        return NO_PROPOSALS;
      }
      unitLibraryFolder = unitLibraryFile.getParentFile();
    }
    // prepare CorrectionUtils
    utils = new CorrectionUtils(unit);
    node = utils.findNode(selectionOffset, ASTNode.class);
    //
    final InstrumentationBuilder instrumentation = Instrumentation.builder(this.getClass());
    try {
      ErrorCode errorCode = problem.getErrorCode();
      if (errorCode == CompileTimeErrorCode.NO_DEFAULT_SUPER_CONSTRUCTOR_EXPLICIT) {
        addFix_createConstructorSuperExplicit();
      }
      if (errorCode == CompileTimeErrorCode.NO_DEFAULT_SUPER_CONSTRUCTOR_IMPLICIT) {
        addFix_createConstructorSuperImplicit();
      }
      if (errorCode == CompileTimeErrorCode.UNDEFINED_CONSTRUCTOR_IN_INITIALIZER_DEFAULT) {
        addFix_createConstructorSuperExplicit();
      }
      if (errorCode == CompileTimeErrorCode.URI_DOES_NOT_EXIST) {
        addFix_createPart();
      }
      if (errorCode == ParserErrorCode.EXPECTED_TOKEN) {
        addFix_insertSemicolon();
      }
      if (errorCode == ParserErrorCode.GETTER_WITH_PARAMETERS) {
        addFix_removeParameters_inGetterDeclaration();
      }
      if (errorCode == StaticWarningCode.CONCRETE_CLASS_WITH_ABSTRACT_MEMBER) {
        addFix_makeEnclosingClassAbstract();
      }
      if (errorCode == StaticWarningCode.NON_ABSTRACT_CLASS_INHERITS_ABSTRACT_MEMBER_ONE
          || errorCode == StaticWarningCode.NON_ABSTRACT_CLASS_INHERITS_ABSTRACT_MEMBER_TWO
          || errorCode == StaticWarningCode.NON_ABSTRACT_CLASS_INHERITS_ABSTRACT_MEMBER_THREE
          || errorCode == StaticWarningCode.NON_ABSTRACT_CLASS_INHERITS_ABSTRACT_MEMBER_FOUR
          || errorCode == StaticWarningCode.NON_ABSTRACT_CLASS_INHERITS_ABSTRACT_MEMBER_FIVE_PLUS) {
        // make class abstract
        addFix_makeEnclosingClassAbstract();
        // implement methods
        AnalysisErrorWithProperties errorWithProperties = (AnalysisErrorWithProperties) problem;
        Object property = errorWithProperties.getProperty(ErrorProperty.UNIMPLEMENTED_METHODS);
        ExecutableElement[] missingOverrides = (ExecutableElement[]) property;
        addFix_createMissingOverrides(missingOverrides);
      }
      if (errorCode == StaticWarningCode.UNDEFINED_CLASS) {
        addFix_importLibrary_withType();
        addFix_createClass();
        addFix_undefinedClass_useSimilar();
      }
      if (errorCode == StaticWarningCode.UNDEFINED_CLASS_BOOLEAN) {
        addFix_boolInsteadOfBoolean();
      }
      if (errorCode == StaticWarningCode.UNDEFINED_IDENTIFIER) {
        addFix_importLibrary_withType();
        addFix_importLibrary_withTopLevelVariable();
      }
      if (errorCode == StaticTypeWarningCode.INVOCATION_OF_NON_FUNCTION) {
        addFix_removeParentheses_inGetterInvocation();
      }
      if (errorCode == StaticTypeWarningCode.UNDEFINED_FUNCTION) {
        addFix_importLibrary_withFunction();
        addFix_undefinedFunction_useSimilar();
      }
      if (errorCode == StaticTypeWarningCode.UNDEFINED_METHOD) {
        addFix_undefinedMethod_useSimilar();
        addFix_undefinedMethodCreate();
      }
      // clean-up
      resetProposalElements();
      // write instrumentation
      instrumentation.metric("QuickFix-Offset", selectionOffset);
      instrumentation.metric("QuickFix-Length", selectionLength);
      instrumentation.metric("QuickFix-ProposalCount", proposals.size());
      instrumentation.data("QuickFix-Source", utils.getText());
      for (int index = 0; index < proposals.size(); index++) {
        instrumentation.data("QuickFix-Proposal-" + index, proposals.get(index).getName());
      }
      // done
      return proposals.toArray(new CorrectionProposal[proposals.size()]);
    } finally {
      instrumentation.log();
    }
  }

  @Override
  public boolean hasFix(AnalysisError problem) {
    ErrorCode errorCode = problem.getErrorCode();
//    System.out.println(errorCode.getClass() + " " + errorCode);
    return errorCode == CompileTimeErrorCode.NO_DEFAULT_SUPER_CONSTRUCTOR_EXPLICIT
        || errorCode == CompileTimeErrorCode.NO_DEFAULT_SUPER_CONSTRUCTOR_IMPLICIT
        || errorCode == CompileTimeErrorCode.UNDEFINED_CONSTRUCTOR_IN_INITIALIZER_DEFAULT
        || errorCode == CompileTimeErrorCode.URI_DOES_NOT_EXIST
        || errorCode == ParserErrorCode.EXPECTED_TOKEN
        || errorCode == ParserErrorCode.GETTER_WITH_PARAMETERS
        || errorCode == StaticWarningCode.CONCRETE_CLASS_WITH_ABSTRACT_MEMBER
        || errorCode == StaticWarningCode.NON_ABSTRACT_CLASS_INHERITS_ABSTRACT_MEMBER_ONE
        || errorCode == StaticWarningCode.NON_ABSTRACT_CLASS_INHERITS_ABSTRACT_MEMBER_TWO
        || errorCode == StaticWarningCode.NON_ABSTRACT_CLASS_INHERITS_ABSTRACT_MEMBER_THREE
        || errorCode == StaticWarningCode.NON_ABSTRACT_CLASS_INHERITS_ABSTRACT_MEMBER_FOUR
        || errorCode == StaticWarningCode.NON_ABSTRACT_CLASS_INHERITS_ABSTRACT_MEMBER_FIVE_PLUS
        || errorCode == StaticWarningCode.UNDEFINED_CLASS
        || errorCode == StaticWarningCode.UNDEFINED_CLASS_BOOLEAN
        || errorCode == StaticWarningCode.UNDEFINED_IDENTIFIER
        || errorCode == StaticTypeWarningCode.INVOCATION_OF_NON_FUNCTION
        || errorCode == StaticTypeWarningCode.UNDEFINED_FUNCTION
        || errorCode == StaticTypeWarningCode.UNDEFINED_METHOD;
  }

  private void addFix_boolInsteadOfBoolean() {
    SourceRange range = rangeError(problem);
    addReplaceEdit(range, "bool");
    addUnitCorrectionProposal(CorrectionKind.QF_REPLACE_BOOLEAN_WITH_BOOL);
  }

  private void addFix_createClass() {
    if (mayBeTypeIdentifier(node)) {
      String name = ((SimpleIdentifier) node).getName();
      // prepare environment
      String eol = utils.getEndOfLine();
      CompilationUnitMember enclosingMember = node.getAncestor(CompilationUnitMember.class);
      int offset = enclosingMember.getEnd();
      String prefix = "";
      // prepare source
      SourceBuilder sb = new SourceBuilder(offset);
      {
        sb.append(eol + eol);
        sb.append(prefix);
        // "class"
        sb.append("class ");
        // append name
        {
          sb.startPosition("NAME");
          sb.append(name);
          sb.endPosition();
        }
        // no members
        sb.append(" {");
        sb.append(eol);
        sb.append("}");
      }
      // insert source
      addInsertEdit(offset, sb.toString());
      // add linked positions
      addLinkedPosition("NAME", rangeNode(node));
      addLinkedPositions(sb);
      // add proposal
      addUnitCorrectionProposal(CorrectionKind.QF_CREATE_CLASS, name);
    }
  }

  /**
   * @see StaticWarningCode#NO_DEFAULT_SUPER_CONSTRUCTOR_EXPLICIT
   */
  private void addFix_createConstructorSuperExplicit() {
    ConstructorDeclaration targetConstructor = (ConstructorDeclaration) node.getParent();
    ClassDeclaration targetClassNode = (ClassDeclaration) targetConstructor.getParent();
    ClassElement targetClassElement = targetClassNode.getElement();
    ClassElement superClassElement = targetClassElement.getSupertype().getElement();
    // add proposals for all super constructors
    ConstructorElement[] superConstructors = superClassElement.getConstructors();
    for (ConstructorElement superConstructor : superConstructors) {
      String constructorName = superConstructor.getName();
      // skip private
      if (SimpleIdentifier.isPrivateName(constructorName)) {
        continue;
      }
      // prepare SourceBuilder
      SourceBuilder sb;
      {
        List<ConstructorInitializer> initializers = targetConstructor.getInitializers();
        if (initializers.isEmpty()) {
          int insertOffset = targetConstructor.getParameters().getEnd();
          sb = new SourceBuilder(insertOffset);
          sb.append(" : ");
        } else {
          ConstructorInitializer lastInitializer = initializers.get(initializers.size() - 1);
          int insertOffset = lastInitializer.getEnd();
          sb = new SourceBuilder(insertOffset);
          sb.append(", ");
        }
      }
      // add super constructor name
      sb.append("super");
      if (!StringUtils.isEmpty(constructorName)) {
        sb.append(".");
        sb.append(constructorName);
      }
      // add arguments
      sb.append("(");
      boolean firstParameter = true;
      for (ParameterElement parameter : superConstructor.getParameters()) {
        // skip non-required parameters
        if (parameter.getParameterKind() != ParameterKind.REQUIRED) {
          break;
        }
        // comma
        if (firstParameter) {
          firstParameter = false;
        } else {
          sb.append(", ");
        }
        // default value
        Type parameterType = parameter.getType();
        sb.startPosition(parameter.getName());
        sb.append(CorrectionUtils.getDefaultValueCode(parameterType));
        sb.endPosition();
      }
      sb.append(")");
      // insert proposal
      addLinkedPositions(sb);
      addInsertEdit(sb);
      // add proposal
      String proposalName = getConstructorProposalName(superConstructor);
      addUnitCorrectionProposal(CorrectionKind.QF_ADD_SUPER_CONSTRUCTOR_INVOCATION, proposalName);
    }
  }

  /**
   * @see StaticWarningCode#NO_DEFAULT_SUPER_CONSTRUCTOR_IMPLICIT
   */
  private void addFix_createConstructorSuperImplicit() {
    ClassDeclaration targetClassNode = (ClassDeclaration) node.getParent();
    ClassElement targetClassElement = targetClassNode.getElement();
    ClassElement superClassElement = targetClassElement.getSupertype().getElement();
    String targetClassName = targetClassElement.getName();
    // add proposals for all super constructors
    ConstructorElement[] superConstructors = superClassElement.getConstructors();
    for (ConstructorElement superConstructor : superConstructors) {
      String constructorName = superConstructor.getName();
      // skip private
      if (SimpleIdentifier.isPrivateName(constructorName)) {
        continue;
      }
      // prepare parameters and arguments
      StringBuilder parametersBuffer = new StringBuilder();
      StringBuilder argumentsBuffer = new StringBuilder();
      boolean firstParameter = true;
      for (ParameterElement parameter : superConstructor.getParameters()) {
        // skip non-required parameters
        if (parameter.getParameterKind() != ParameterKind.REQUIRED) {
          break;
        }
        // comma
        if (firstParameter) {
          firstParameter = false;
        } else {
          parametersBuffer.append(", ");
          argumentsBuffer.append(", ");
        }
        // name
        String parameterName = parameter.getDisplayName();
        if (parameterName.length() > 1 && parameterName.startsWith("_")) {
          parameterName = parameterName.substring(1);
        }
        // parameter & argument
        appendParameterSource(parametersBuffer, parameter.getType(), parameterName);
        argumentsBuffer.append(parameterName);
      }
      // add proposal
      StringBuilder sb = new StringBuilder();
      {
        String eol = utils.getEndOfLine();
        String indent = utils.getIndent(1);
        sb.append(eol);
        sb.append(indent);
        sb.append(targetClassName);
        sb.append("(");
        sb.append(parametersBuffer);
        sb.append(") : super");
        if (!constructorName.isEmpty()) {
          sb.append(".");
          sb.append(constructorName);
        }
        sb.append("(");
        sb.append(argumentsBuffer);
        sb.append(");");
        if (!targetClassNode.getMembers().isEmpty()) {
          sb.append(eol);
        }
      }
      addInsertEdit(targetClassNode.getLeftBracket().getEnd(), sb.toString());
      // add proposal
      String proposalName = getConstructorProposalName(superConstructor);
      addUnitCorrectionProposal(CorrectionKind.QF_CREATE_CONSTRUCTOR_SUPER, proposalName);
    }
  }

  private void addFix_createMissingOverrides(ExecutableElement[] missingOverrides) throws Exception {
    // sort by name
    Arrays.sort(missingOverrides, new Comparator<Element>() {
      @Override
      public int compare(Element firstElement, Element secondElement) {
        return ObjectUtils.compare(firstElement.getDisplayName(), secondElement.getDisplayName());
      }
    });
    // add elements
    ClassDeclaration targetClass = (ClassDeclaration) node.getParent();
    boolean isFirst = true;
    for (ExecutableElement missingOverride : missingOverrides) {
      addFix_createMissingOverrides_single(targetClass, missingOverride, isFirst);
      isFirst = false;
    }
    // add proposal
    addUnitCorrectionProposal(CorrectionKind.QF_CREATE_MISSING_OVERRIDES, missingOverrides.length);
  }

  private void addFix_createMissingOverrides_single(ClassDeclaration targetClass,
      ExecutableElement missingOverride, boolean isFirst) throws Exception {
    // prepare environment
    String eol = utils.getEndOfLine();
    String prefix = utils.getIndent(1);
    String prefix2 = utils.getIndent(2);
    int insertOffset = targetClass.getEnd() - 1;
    // prepare source
    StringBuilder sb = new StringBuilder();
    // may be empty line
    if (!isFirst || !targetClass.getMembers().isEmpty()) {
      sb.append(eol);
    }
    // return type
    sb.append(prefix);
    appendType(sb, missingOverride.getType().getReturnType());
    // may be property
    ElementKind elementKind = missingOverride.getKind();
    boolean isGetter = elementKind == ElementKind.GETTER;
    boolean isSetter = elementKind == ElementKind.SETTER;
    boolean isMethod = elementKind == ElementKind.METHOD;
    boolean isOperator = isMethod && ((MethodElement) missingOverride).isOperator();
    if (isGetter) {
      sb.append("get ");
    } else if (isSetter) {
      sb.append("set ");
    } else if (isOperator) {
      sb.append("operator ");
    }
    // name
    sb.append(missingOverride.getDisplayName());
    // parameters + body
    if (isGetter) {
      sb.append(" => null; // TODO implement this getter");
    } else if (isMethod || isSetter) {
      ParameterElement[] parameters = missingOverride.getParameters();
      appendParameters(sb, parameters);
      sb.append(" {");
      // TO-DO
      sb.append(eol);
      sb.append(prefix2);
      if (isMethod) {
        sb.append("// TODO implement this method");
      } else {
        sb.append("// TODO implement this setter");
      }
      sb.append(eol);
      // close method
      sb.append(prefix);
      sb.append("}");
    }
    sb.append(eol);
    // done
    addInsertEdit(insertOffset, sb.toString());
  }

  private void addFix_createPart() throws Exception {
    if (node instanceof SimpleStringLiteral && node.getParent() instanceof PartDirective) {
      SimpleStringLiteral uriLiteral = (SimpleStringLiteral) node;
      String uriString = uriLiteral.getValue();
      // prepare referenced File
      File newFile;
      {
        URI uri = URI.create(uriString);
        if (uri.isAbsolute()) {
          return;
        }
        newFile = new File(unitLibraryFolder, uriString);
      }
      if (!newFile.exists()) {
        // prepare new source
        String source;
        {
          String eol = utils.getEndOfLine();
          String libraryName = unitLibraryElement.getDisplayName();
          source = "part of " + libraryName + ";" + eol + eol;
        }
        // add proposal
        proposals.add(new CreateFileCorrectionProposal(
            newFile,
            source,
            CorrectionKind.QF_CREATE_PART,
            uriString));
      }
    }
  }

  private void addFix_importLibrary(CorrectionKind kind, String importPath) throws Exception {
    CompilationUnitElement libraryUnitElement = unitLibraryElement.getDefiningCompilationUnit();
    CompilationUnit libraryUnit = CorrectionUtils.getResolvedUnit(libraryUnitElement);
    // prepare new import location
    int offset = 0;
    String prefix;
    String suffix;
    {
      String eol = utils.getEndOfLine();
      // if no directives
      prefix = "";
      suffix = eol;
      CorrectionUtils libraryUtils = new CorrectionUtils(libraryUnit);
      // after last directive in library
      for (Directive directive : libraryUnit.getDirectives()) {
        if (directive instanceof LibraryDirective || directive instanceof ImportDirective) {
          offset = directive.getEnd();
          prefix = eol;
          suffix = "";
        }
      }
      // if still beginning of file, skip shebang and line comments
      if (offset == 0) {
        InsertDesc desc = libraryUtils.getInsertDescTop();
        offset = desc.offset;
        prefix = desc.prefix;
        suffix = desc.suffix + eol;
      }
    }
    // insert new import
    String importSource = prefix + "import '" + importPath + "';" + suffix;
    addInsertEdit(offset, importSource);
    // add proposal
    addUnitCorrectionProposal(libraryUnitElement.getSource(), kind, importPath);
  }

  private void addFix_importLibrary_withElement(String name, ElementKind kind) throws Exception {
    // ignore if private
    if (name.startsWith("_")) {
      return;
    }
    // may be there is existing import, but it is with prefix and we don't use this prefix
    for (ImportElement imp : unitLibraryElement.getImports()) {
      // prepare prefix
      PrefixElement prefix = imp.getPrefix();
      if (prefix == null) {
        continue;
      }
      // prepare element
      LibraryElement libraryElement = imp.getImportedLibrary();
      Element element = CorrectionUtils.getExportedElement(libraryElement, name);
      if (element == null) {
        continue;
      }
      if (element.getKind() != kind) {
        continue;
      }
      // insert prefix
      SourceRange range = rangeStartLength(node, 0);
      addReplaceEdit(range, prefix.getDisplayName() + ".");
      addUnitCorrectionProposal(
          CorrectionKind.QF_IMPORT_LIBRARY_PREFIX,
          libraryElement.getDisplayName(),
          prefix.getDisplayName());
    }
    // check SDK libraries
    AnalysisContext context = unitLibraryElement.getContext();
    {
      DartSdk sdk = context.getSourceFactory().getDartSdk();
      AnalysisContext sdkContext = sdk.getContext();
      SdkLibrary[] sdkLibraries = sdk.getSdkLibraries();
      for (SdkLibrary sdkLibrary : sdkLibraries) {
        SourceFactory sdkSourceFactory = sdkContext.getSourceFactory();
        String libraryUri = sdkLibrary.getShortName();
        Source librarySource = sdkSourceFactory.resolveUri(null, libraryUri);
        // prepare LibraryElement
        LibraryElement libraryElement = sdkContext.getLibraryElement(librarySource);
        if (libraryElement == null) {
          continue;
        }
        // prepare exported Element
        Element element = CorrectionUtils.getExportedElement(libraryElement, name);
        if (element == null) {
          continue;
        }
        if (element.getKind() != kind) {
          continue;
        }
        // add import
        addFix_importLibrary(CorrectionKind.QF_IMPORT_LIBRARY_SDK, libraryUri);
      }
    }
    // check project libraries
    {
      Source[] librarySources = context.getLibrarySources();
      for (Source librarySource : librarySources) {
        // we don't need SDK libraries here
        if (librarySource.isInSystemLibrary()) {
          continue;
        }
        // prepare LibraryElement
        LibraryElement libraryElement = context.getLibraryElement(librarySource);
        if (libraryElement == null) {
          continue;
        }
        // prepare exported Element
        Element element = CorrectionUtils.getExportedElement(libraryElement, name);
        if (element == null) {
          continue;
        }
        if (element.getKind() != kind) {
          continue;
        }
        // prepare "library" file
        File libraryFile = getSourceFile(librarySource);
        if (libraryFile == null) {
          continue;
        }
        // may be "package:" URI
        {
          URI libraryPackageUri = findPackageUri(context, libraryFile);
          if (libraryPackageUri != null) {
            addFix_importLibrary(
                CorrectionKind.QF_IMPORT_LIBRARY_PROJECT,
                libraryPackageUri.toString());
            continue;
          }
        }
        // relative URI
        URI unitLibraryUri = unitLibraryFolder.toURI();
        URI libraryUri = libraryFile.toURI();
        String relative = unitLibraryUri.relativize(libraryUri).getPath();
        addFix_importLibrary(CorrectionKind.QF_IMPORT_LIBRARY_PROJECT, relative);
      }
    }
  }

  private void addFix_importLibrary_withFunction() throws Exception {
    if (node instanceof SimpleIdentifier && node.getParent() instanceof MethodInvocation) {
      MethodInvocation invocation = (MethodInvocation) node.getParent();
      if (invocation.getRealTarget() == null && invocation.getMethodName() == node) {
        String name = ((SimpleIdentifier) node).getName();
        addFix_importLibrary_withElement(name, ElementKind.FUNCTION);
      }
    }
  }

  private void addFix_importLibrary_withTopLevelVariable() throws Exception {
    if (node instanceof SimpleIdentifier) {
      String name = ((SimpleIdentifier) node).getName();
      addFix_importLibrary_withElement(name, ElementKind.TOP_LEVEL_VARIABLE);
    }
  }

  private void addFix_importLibrary_withType() throws Exception {
    if (mayBeTypeIdentifier(node)) {
      String typeName = ((SimpleIdentifier) node).getName();
      addFix_importLibrary_withElement(typeName, ElementKind.CLASS);
    }
  }

  private void addFix_insertSemicolon() {
    if (problem.getMessage().contains("';'")) {
      int insertOffset = problem.getOffset() + problem.getLength();
      addInsertEdit(insertOffset, ";");
      addUnitCorrectionProposal(CorrectionKind.QF_INSERT_SEMICOLON);
    }
  }

  private void addFix_makeEnclosingClassAbstract() {
    ClassDeclaration enclosingClass = node.getAncestor(ClassDeclaration.class);
    String className = enclosingClass.getName().getName();
    addInsertEdit(enclosingClass.getClassKeyword().getOffset(), "abstract ");
    addUnitCorrectionProposal(CorrectionKind.QF_MAKE_CLASS_ABSTRACT, className);
  }

  private void addFix_removeParameters_inGetterDeclaration() throws Exception {
    if (node instanceof SimpleIdentifier && node.getParent() instanceof MethodDeclaration) {
      MethodDeclaration method = (MethodDeclaration) node.getParent();
      FunctionBody body = method.getBody();
      if (method.getName() == node && body != null) {
        addReplaceEdit(rangeEndStart(node, body), " ");
        addUnitCorrectionProposal(CorrectionKind.QF_REMOVE_PARAMETERS_IN_GETTER_DECLARATION);
      }
    }
  }

  private void addFix_removeParentheses_inGetterInvocation() throws Exception {
    if (node instanceof SimpleIdentifier && node.getParent() instanceof MethodInvocation) {
      MethodInvocation invocation = (MethodInvocation) node.getParent();
      if (invocation.getMethodName() == node && invocation.getTarget() != null) {
        addRemoveEdit(rangeEndEnd(node, invocation));
        addUnitCorrectionProposal(CorrectionKind.QF_REMOVE_PARENTHESIS_IN_GETTER_INVOCATION);
      }
    }
  }

  private void addFix_undefinedClass_useSimilar() {
    if (mayBeTypeIdentifier(node)) {
      String name = ((SimpleIdentifier) node).getName();
      final ClosestElementFinder finder = new ClosestElementFinder(ClassElement.class, name);
      // find closest element
      {
        // elements of this library
        unitLibraryElement.accept(new RecursiveElementVisitor<Void>() {
          @Override
          public Void visitClassElement(ClassElement element) {
            finder.update(element);
            return null;
          }
        });
        // elements from imports
        for (ImportElement importElement : unitLibraryElement.getImports()) {
          if (importElement.getPrefix() == null) {
            Map<String, Element> namespace = CorrectionUtils.getImportNamespace(importElement);
            finder.update(namespace.values());
          }
        }
      }
      // if we have close enough element, suggest to use it
      if (finder != null && finder.distance < 5) {
        String closestName = finder.element.getName();
        addReplaceEdit(rangeNode(node), closestName);
        // add proposal
        if (closestName != null) {
          addUnitCorrectionProposal(CorrectionKind.QF_CHANGE_TO, closestName);
        }
      }
    }
  }

  private void addFix_undefinedFunction_useSimilar() throws Exception {
    if (node instanceof SimpleIdentifier) {
      String name = ((SimpleIdentifier) node).getName();
      final ClosestElementFinder finder = new ClosestElementFinder(FunctionElement.class, name);
      // this library
      unitLibraryElement.accept(new RecursiveElementVisitor<Void>() {
        @Override
        public Void visitFunctionElement(FunctionElement element) {
          finder.update(element);
          return null;
        }
      });
      // imports
      for (ImportElement importElement : unitLibraryElement.getImports()) {
        if (importElement.getPrefix() == null) {
          Map<String, Element> namespace = CorrectionUtils.getImportNamespace(importElement);
          finder.update(namespace.values());
        }
      }
      // if we have close enough element, suggest to use it
      String closestName = null;
      if (finder != null && finder.distance < 5) {
        closestName = finder.element.getName();
        addReplaceEdit(rangeNode(node), closestName);
        addUnitCorrectionProposal(CorrectionKind.QF_CHANGE_TO, closestName);
      }
    }
  }

  private void addFix_undefinedMethod_useSimilar() throws Exception {
    if (node instanceof SimpleIdentifier && node.getParent() instanceof MethodInvocation) {
      MethodInvocation invocation = (MethodInvocation) node.getParent();
      String name = ((SimpleIdentifier) node).getName();
      ClosestElementFinder finder = new ClosestElementFinder(MethodElement.class, name);
      // unqualified invocation
      Expression target = invocation.getRealTarget();
      if (target == null) {
        ClassDeclaration clazz = invocation.getAncestor(ClassDeclaration.class);
        if (clazz != null) {
          ClassElement clazzElement = clazz.getElement();
          updateFinderWithClassMembers(finder, clazzElement);
        }
      } else {
        Type type = typeOf(target);
        if (type instanceof InterfaceType) {
          ClassElement clazzElement = ((InterfaceType) type).getElement();
          updateFinderWithClassMembers(finder, clazzElement);
        }
      }
      // if we have close enough element, suggest to use it
      String closestName = null;
      if (finder != null && finder.distance < 5) {
        closestName = finder.element.getName();
        addReplaceEdit(rangeNode(node), closestName);
        addUnitCorrectionProposal(CorrectionKind.QF_CHANGE_TO, closestName);
      }
    }
  }

  private void addFix_undefinedMethodCreate() throws Exception {
    if (node instanceof SimpleIdentifier && node.getParent() instanceof MethodInvocation) {
      String name = ((SimpleIdentifier) node).getName();
      MethodInvocation invocation = (MethodInvocation) node.getParent();
      // prepare environment
      String eol = utils.getEndOfLine();
      Source targetSource;
      String prefix;
      int insertOffset;
      String sourcePrefix;
      String sourceSuffix;
      boolean staticModifier = false;
      Expression target = invocation.getRealTarget();
      if (target == null) {
        targetSource = source;
        ClassMember enclosingMember = node.getAncestor(ClassMember.class);
        if (enclosingMember instanceof MethodDeclaration) {
          staticModifier = ((MethodDeclaration) enclosingMember).isStatic();
        }
        if (enclosingMember instanceof FieldDeclaration) {
          staticModifier = ((FieldDeclaration) enclosingMember).isStatic();
        }
        prefix = utils.getNodePrefix(enclosingMember);
        insertOffset = enclosingMember.getEnd();
        sourcePrefix = eol + prefix + eol;
        sourceSuffix = "";
      } else {
        Type targetType = typeOf(target);
        Element targetElement = targetType.getElement();
        targetSource = targetElement.getSource();
        // may be static
        if (target instanceof Identifier) {
          staticModifier = ((Identifier) target).getElement().getKind() == ElementKind.CLASS;
        }
        // prepare insert offset
        ClassDeclaration targetClass = CorrectionUtils.getResolvedNode(targetElement);
        prefix = "  ";
        insertOffset = targetClass.getEnd() - 1;
        if (targetClass.getMembers().isEmpty()) {
          sourcePrefix = "";
        } else {
          sourcePrefix = prefix + eol;
        }
        sourceSuffix = eol;
      }
      // build method source
      SourceBuilder sb = new SourceBuilder(insertOffset);
      {
        sb.append(sourcePrefix);
        sb.append(prefix);
        // may be "static"
        if (staticModifier) {
          sb.append("static ");
        }
        // may be return type
        {
          Type type = addFix_undefinedMethodCreate_getReturnType(invocation);
          if (type != null) {
            String typeSource = utils.getTypeSource(type);
            if (!typeSource.equals("dynamic")) {
              sb.startPosition("RETURN_TYPE");
              sb.append(typeSource);
              sb.endPosition();
              sb.append(" ");
            }
          }
        }
        // append name
        {
          sb.startPosition("NAME");
          sb.append(name);
          sb.endPosition();
        }
        addFix_undefinedMethodCreate_parameters(sb, invocation);
        sb.append(") {" + eol + prefix + "}");
        sb.append(sourceSuffix);
      }
      // insert source
      addInsertEdit(insertOffset, sb.toString());
      // add linked positions
      if (Objects.equal(targetSource, source)) {
        addLinkedPosition("NAME", sb, rangeNode(node));
      }
      addLinkedPositions(sb);
      // add proposal
      addUnitCorrectionProposal(targetSource, CorrectionKind.QF_CREATE_METHOD, name);
    }
  }

  /**
   * @return the possible return {@link Type}, may be <code>null</code> if can not be identified.
   */
  private Type addFix_undefinedMethodCreate_getReturnType(MethodInvocation invocation) {
    if (invocation.getParent() instanceof VariableDeclaration) {
      VariableDeclaration variableDeclaration = (VariableDeclaration) invocation.getParent();
      if (variableDeclaration.getInitializer() == invocation) {
        VariableElement variableElement = variableDeclaration.getElement();
        if (variableElement != null) {
          return variableElement.getType();
        }
      }
    }
    return null;
  }

  private void addFix_undefinedMethodCreate_parameters(SourceBuilder sb, MethodInvocation invocation) {
    // append parameters
    sb.append("(");
    Set<String> excluded = Sets.newHashSet();
    List<Expression> arguments = invocation.getArgumentList().getArguments();
    for (int i = 0; i < arguments.size(); i++) {
      Expression argument = arguments.get(i);
      // append separator
      if (i != 0) {
        sb.append(", ");
      }
      // append type name
      Type type = typeOf(argument);
      if (type != null) {
        String typeSource = utils.getTypeSource(type);
        {
          sb.startPosition("TYPE" + i);
          sb.append(typeSource);
          addSuperTypeProposals(sb, Sets.<Type> newHashSet(), type);
          sb.endPosition();
        }
        sb.append(" ");
      }
      // append parameter name
      {
        String[] suggestions = getArgumentNameSuggestions(excluded, type, argument, i);
        String favorite = suggestions[0];
        excluded.add(favorite);
        sb.startPosition("ARG" + i);
        sb.append(favorite);
        sb.setProposals(suggestions);
        sb.endPosition();
      }
    }
  }

  // TODO(scheglov) waiting for https://code.google.com/p/dart/issues/detail?id=10053
//  private void addFix_useEffectiveIntegerDivision(IProblemLocation location) throws Exception {
//    for (DartNode n = node; n != null; n = n.getParent()) {
//      if (n instanceof DartMethodInvocation
//          && n.getSourceInfo().getOffset() == location.getOffset()
//          && n.getSourceInfo().getLength() == location.getLength()) {
//        DartMethodInvocation invocation = (DartMethodInvocation) n;
//        DartExpression target = invocation.getTarget();
//        while (target instanceof DartParenthesizedExpression) {
//          target = ((DartParenthesizedExpression) target).getExpression();
//        }
//        // replace "/" with "~/"
//        DartBinaryExpression binary = (DartBinaryExpression) target;
//        addReplaceEdit(
//            SourceRangeFactory.forStartLength(binary.getOperatorOffset(), "/".length()),
//            "~/");
//        // remove everything before and after
//        addRemoveEdit(SourceRangeFactory.forStartStart(invocation, binary.getArg1()));
//        addRemoveEdit(SourceRangeFactory.forEndEnd(binary.getArg2(), invocation));
//        // add proposal
//        addUnitCorrectionProposal(
//            CorrectionMessages.QuickFixProcessor_useEffectiveIntegerDivision,
//            DartPluginImages.get(DartPluginImages.IMG_CORRECTION_CHANGE));
//        // done
//        break;
//      }
//    }
//  }

  private void addInsertEdit(int offset, String text) {
    textEdits.add(createInsertEdit(offset, text));
  }

  private void addInsertEdit(SourceBuilder builder) {
    addInsertEdit(builder.getOffset(), builder.toString());
  }

  /**
   * Adds single linked position to the group. If {@link SourceBuilder} will be inserted before
   * "position", translate it.
   */
  private void addLinkedPosition(String group, SourceBuilder sb, SourceRange position) {
    if (sb.getOffset() < position.getOffset()) {
      int delta = sb.length();
      position = position.getTranslated(delta);
    }
    addLinkedPosition(group, position);
  }

  /**
   * Adds single linked position to the group.
   */
  private void addLinkedPosition(String group, SourceRange position) {
    List<SourceRange> positions = linkedPositions.get(group);
    if (positions == null) {
      positions = Lists.newArrayList();
      linkedPositions.put(group, positions);
    }
    positions.add(position);
  }

  private void addLinkedPositionProposal(String group, LinkedPositionProposal proposal) {
    List<LinkedPositionProposal> nodeProposals = linkedPositionProposals.get(group);
    if (nodeProposals == null) {
      nodeProposals = Lists.newArrayList();
      linkedPositionProposals.put(group, nodeProposals);
    }
    nodeProposals.add(proposal);
  }

  // https://code.google.com/p/dart/issues/detail?id=10058
  // TODO(scheglov) implement this
//  private void addFix_useStaticAccess_method() throws Exception {
//    if (getLocationInParent(node) == DART_METHOD_INVOCATION_FUNCTION_NAME) {
//      DartMethodInvocation invocation = (DartMethodInvocation) node.getParent();
//      Element methodElement = node.getElement();
//      if (methodElement instanceof MethodElement
//          && methodElement.getEnclosingElement() instanceof ClassElement) {
//        ClassElement classElement = (ClassElement) methodElement.getEnclosingElement();
//        String className = classElement.getName();
//        // if has this class in current library, use name as is
//        if (unit.getLibrary().findType(className) != null) {
//          addFix_useStaticAccess_method_proposal(invocation, className);
//          return;
//        }
//        // class from other library, may be use prefix
//        for (DartImport imp : unit.getLibrary().getImports()) {
//          if (imp.getLibrary().findType(className) != null) {
//            className = imp.getPrefix() + "." + className;
//            addFix_useStaticAccess_method_proposal(invocation, className);
//          }
//        }
//      }
//    }
//  }

//  private void addFix_useStaticAccess_method_proposal(MethodInvocation invocation, String className) {
//    DartExpression target = invocation.getTarget();
//    if (target == null) {
//      return;
//    }
//    // replace "target" with class name
//    SourceRange range = SourceRangeFactory.create(target);
//    addReplaceEdit(range, className);
//    // add proposal
//    addUnitCorrectionProposal(
//        Messages.format(CorrectionMessages.QuickFixProcessor_useStaticAccess_method, className),
//        DartPluginImages.get(DartPluginImages.IMG_CORRECTION_CHANGE));
//  }

  /**
   * Adds positions from the given {@link SourceBuilder} to the {@link #linkedPositions}.
   */
  private void addLinkedPositions(SourceBuilder builder) {
    // positions
    for (Entry<String, List<SourceRange>> linkedEntry : builder.getLinkedPositions().entrySet()) {
      String group = linkedEntry.getKey();
      for (SourceRange position : linkedEntry.getValue()) {
        addLinkedPosition(group, position);
      }
    }
    // proposals for positions
    for (Entry<String, List<LinkedPositionProposal>> entry : builder.getLinkedProposals().entrySet()) {
      String group = entry.getKey();
      for (LinkedPositionProposal proposal : entry.getValue()) {
        addLinkedPositionProposal(group, proposal);
      }
    }
  }

  private void addRemoveEdit(SourceRange range) {
    textEdits.add(createRemoveEdit(range));
  }

  /**
   * Adds {@link Edit} to {@link #textEdits}.
   */
  private void addReplaceEdit(SourceRange range, String text) {
    textEdits.add(createReplaceEdit(range, text));
  }

  /**
   * Adds {@link CorrectionProposal} with single {@link SourceChange} to {@link #proposals}.
   */
  private void addUnitCorrectionProposal(CorrectionKind kind, Object... arguments) {
    addUnitCorrectionProposal(source, kind, arguments);
  }

  /**
   * Adds {@link CorrectionProposal} with single {@link SourceChange} to {@link #proposals}.
   */
  private void addUnitCorrectionProposal(Source source, CorrectionKind kind, Object... arguments) {
    if (!textEdits.isEmpty()) {
      // prepare SourceChange
      SourceChange change = new SourceChange(source.getShortName(), source);
      for (Edit edit : textEdits) {
        change.addEdit(edit);
      }
      // create SourceCorrectionProposal
      SourceCorrectionProposal proposal = new SourceCorrectionProposal(change, kind, arguments);
      proposal.setLinkedPositions(linkedPositions);
      proposal.setLinkedPositionProposals(linkedPositionProposals);
      // done
      proposal.setLinkedPositions(linkedPositions);
      proposal.setLinkedPositionProposals(linkedPositionProposals);
      proposals.add(proposal);
    }
    // reset
    resetProposalElements();
  }

  private void appendParameters(StringBuilder sb, ParameterElement[] parameters) throws Exception {
    Map<ParameterElement, String> defaultValueMap = getDefaultValueMap(parameters);
    appendParameters(sb, parameters, defaultValueMap);
  }

  private void appendParameters(StringBuilder sb, ParameterElement[] parameters,
      Map<ParameterElement, String> defaultValueMap) {
    sb.append("(");
    boolean firstParameter = true;
    boolean sawNamed = false;
    boolean sawPositional = false;
    for (ParameterElement parameter : parameters) {
      if (!firstParameter) {
        sb.append(", ");
      } else {
        firstParameter = false;
      }
      // may be optional
      ParameterKind parameterKind = parameter.getParameterKind();
      if (parameterKind == ParameterKind.NAMED) {
        if (!sawNamed) {
          sb.append("{");
          sawNamed = true;
        }
      }
      if (parameterKind == ParameterKind.POSITIONAL) {
        if (!sawPositional) {
          sb.append("[");
          sawPositional = true;
        }
      }
      // parameter
      appendParameterSource(sb, parameter.getType(), parameter.getName());
      // default value
      if (defaultValueMap != null) {
        String defaultSource = defaultValueMap.get(parameter);
        if (defaultSource != null) {
          if (sawPositional) {
            sb.append(" = ");
          } else {
            sb.append(": ");
          }
          sb.append(defaultSource);
        }
      }
    }
    // close parameters
    if (sawNamed) {
      sb.append("}");
    }
    if (sawPositional) {
      sb.append("]");
    }
    sb.append(")");
  }

  private void appendParameterSource(StringBuilder sb, Type type, String name) {
    String parameterSource = utils.getParameterSource(type, name);
    sb.append(parameterSource);
  }

  private void appendType(StringBuilder sb, Type type) {
    if (type != null && !type.isDynamic()) {
      String typeSource = utils.getTypeSource(type);
      sb.append(typeSource);
      sb.append(" ");
    }
  }

  private Edit createInsertEdit(int offset, String text) {
    return new Edit(offset, 0, text);
  }

  /**
   * @return the string to display as the name of the given constructor in a proposal name.
   */
  private String getConstructorProposalName(ConstructorElement constructor) {
    StringBuilder proposalNameBuffer = new StringBuilder();
    proposalNameBuffer.append("super");
    // may be named
    String constructorName = constructor.getDisplayName();
    if (!constructorName.isEmpty()) {
      proposalNameBuffer.append(".");
      proposalNameBuffer.append(constructorName);
    }
    // parameters
    appendParameters(proposalNameBuffer, constructor.getParameters(), null);
    // done
    return proposalNameBuffer.toString();
  }

  private Map<ParameterElement, String> getDefaultValueMap(ParameterElement[] parameters)
      throws Exception {
    Map<ParameterElement, String> defaultSourceMap = Maps.newHashMap();
    Map<Source, String> sourceContentMap = Maps.newHashMap();
    for (ParameterElement parameter : parameters) {
      SourceRange valueRange = parameter.getDefaultValueRange();
      if (valueRange != null) {
        Source source = parameter.getSource();
        String sourceContent = sourceContentMap.get(source);
        if (sourceContent == null) {
          sourceContent = CorrectionUtils.getSourceContent(source);
          sourceContentMap.put(source, sourceContent);
        }
        String valueSource = sourceContent.substring(valueRange.getOffset(), valueRange.getEnd());
        defaultSourceMap.put(parameter, valueSource);
      }
    }
    return defaultSourceMap;
  }

  private void resetProposalElements() {
    textEdits.clear();
    linkedPositions.clear();
    positionStopEdits.clear();
    linkedPositionProposals.clear();
//    proposalEndRange = null;
  }

  private void updateFinderWithClassMembers(final ClosestElementFinder finder,
      ClassElement clazzElement) {
    if (clazzElement != null) {
      List<Element> members = HierarchyUtils.getMembers(clazzElement, false);
      finder.update(members);
    }
  }
}
