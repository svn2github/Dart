/*
 * Copyright (c) 2012, the Dart project authors.
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
package com.google.dart.engine.internal.type;

import com.google.dart.engine.EngineTestCase;
import com.google.dart.engine.element.ClassElement;
import com.google.dart.engine.internal.element.ClassElementImpl;
import com.google.dart.engine.internal.element.TypeVariableElementImpl;
import com.google.dart.engine.type.InterfaceType;
import com.google.dart.engine.type.Type;

import static com.google.dart.engine.ast.ASTFactory.identifier;
import static com.google.dart.engine.element.ElementFactory.classElement;

public class InterfaceTypeImplTest extends EngineTestCase {

  public void test_creation() {
    assertNotNull(new InterfaceTypeImpl(new ClassElementImpl(identifier("A"))));
  }

  public void test_getDynamic() {
    assertNotNull(InterfaceTypeImpl.getDynamic());
  }

  public void test_getElement() {
    ClassElementImpl typeElement = new ClassElementImpl(identifier("A"));
    InterfaceTypeImpl type = new InterfaceTypeImpl(typeElement);
    assertEquals(typeElement, type.getElement());
  }

  public void test_getTypeArguments() {
    InterfaceTypeImpl type = new InterfaceTypeImpl(new ClassElementImpl(identifier("A")));
    assertLength(0, type.getTypeArguments());
  }

  public void test_isDirectSupertypeOf_false() {
    ClassElementImpl elementA = new ClassElementImpl(identifier("A"));
    ClassElementImpl elementB = new ClassElementImpl(identifier("B"));
    ClassElementImpl elementC = new ClassElementImpl(identifier("C"));
    InterfaceTypeImpl typeA = new InterfaceTypeImpl(elementA);
    InterfaceTypeImpl typeB = new InterfaceTypeImpl(elementB);
    InterfaceTypeImpl typeC = new InterfaceTypeImpl(elementC);
    elementC.setSupertype(typeB);
    assertFalse(typeA.isDirectSupertypeOf(typeC));
  }

  public void test_isDirectSupertypeOf_true() {
    ClassElementImpl elementA = new ClassElementImpl(identifier("A"));
    ClassElementImpl elementB = new ClassElementImpl(identifier("B"));
    InterfaceTypeImpl typeA = new InterfaceTypeImpl(elementA);
    InterfaceTypeImpl typeB = new InterfaceTypeImpl(elementB);
    elementB.setSupertype(typeA);
    assertTrue(typeA.isDirectSupertypeOf(typeB));
  }

  public void test_isMoreSpecificThan_bottom() {
    InterfaceTypeImpl type = new InterfaceTypeImpl(new ClassElementImpl(identifier("A")));
    assertTrue(BottomTypeImpl.getInstance().isMoreSpecificThan(type));
  }

  public void test_isMoreSpecificThan_covariance() {
    ClassElement elementA = classElement("A", "E");
    ClassElement elementI = classElement("I");
    ClassElement elementJ = classElement("J", elementI.getType());
    InterfaceTypeImpl typeAI = new InterfaceTypeImpl(elementA);
    InterfaceTypeImpl typeAJ = new InterfaceTypeImpl(elementA);
    typeAI.setTypeArguments(new Type[] {elementI.getType()});
    typeAJ.setTypeArguments(new Type[] {elementJ.getType()});
    assertTrue(typeAJ.isMoreSpecificThan(typeAI));
    assertFalse(typeAI.isMoreSpecificThan(typeAJ));
  }

  public void test_isMoreSpecificThan_directSupertype() {
    ClassElementImpl elementA = new ClassElementImpl(identifier("A"));
    ClassElementImpl elementB = new ClassElementImpl(identifier("B"));
    InterfaceTypeImpl typeA = new InterfaceTypeImpl(elementA);
    InterfaceTypeImpl typeB = new InterfaceTypeImpl(elementB);
    elementB.setSupertype(typeA);
    assertTrue(typeB.isMoreSpecificThan(typeA));
    // the opposite test tests a different branch in isMoreSpecificThan()
    assertFalse(typeA.isMoreSpecificThan(typeB));
  }

  public void test_isMoreSpecificThan_dynamic() {
    InterfaceTypeImpl type = new InterfaceTypeImpl(new ClassElementImpl(identifier("A")));
    assertTrue(type.isMoreSpecificThan(InterfaceTypeImpl.getDynamic()));
  }

  public void test_isMoreSpecificThan_indirectSupertype() {
    ClassElementImpl elementA = new ClassElementImpl(identifier("A"));
    ClassElementImpl elementB = new ClassElementImpl(identifier("B"));
    ClassElementImpl elementC = new ClassElementImpl(identifier("C"));
    InterfaceTypeImpl typeA = new InterfaceTypeImpl(elementA);
    InterfaceTypeImpl typeB = new InterfaceTypeImpl(elementB);
    InterfaceTypeImpl typeC = new InterfaceTypeImpl(elementC);
    elementB.setSupertype(typeA);
    elementC.setSupertype(typeB);
    assertTrue(typeC.isMoreSpecificThan(typeA));
  }

  public void test_isMoreSpecificThan_same() {
    InterfaceTypeImpl type = new InterfaceTypeImpl(new ClassElementImpl(identifier("A")));
    assertTrue(type.isMoreSpecificThan(type));
  }

  public void test_setTypeArguments() {
    InterfaceTypeImpl type = new InterfaceTypeImpl(new ClassElementImpl(identifier("A")));
    Type[] typeArguments = new Type[] {
        new InterfaceTypeImpl(new ClassElementImpl(identifier("B"))),
        new InterfaceTypeImpl(new ClassElementImpl(identifier("C"))),};
    type.setTypeArguments(typeArguments);
    assertEquals(typeArguments, type.getTypeArguments());
  }

  public void test_substitute_equal() {
    ClassElementImpl classElement = new ClassElementImpl(identifier("A"));
    TypeVariableElementImpl parameterElement = new TypeVariableElementImpl(identifier("E"));
    //classElement.setTypeVariables(new TypeVariableElement[] {parameterElement});

    InterfaceTypeImpl type = new InterfaceTypeImpl(classElement);
    TypeVariableTypeImpl parameter = new TypeVariableTypeImpl(parameterElement);
    type.setTypeArguments(new Type[] {parameter});

    InterfaceTypeImpl argumentType = new InterfaceTypeImpl(new ClassElementImpl(identifier("B")));

    InterfaceType result = type.substitute(new Type[] {argumentType}, new Type[] {parameter});
    assertEquals(classElement, result.getElement());
    Type[] resultArguments = result.getTypeArguments();
    assertLength(1, resultArguments);
    assertEquals(argumentType, resultArguments[0]);
  }

  public void test_substitute_notEqual() {
    ClassElementImpl classElement = new ClassElementImpl(identifier("A"));
    TypeVariableElementImpl parameterElement = new TypeVariableElementImpl(identifier("E"));
    //classElement.setTypeVariables(new TypeVariableElement[] {parameterElement});

    InterfaceTypeImpl type = new InterfaceTypeImpl(classElement);
    TypeVariableTypeImpl parameter = new TypeVariableTypeImpl(parameterElement);
    type.setTypeArguments(new Type[] {parameter});

    InterfaceTypeImpl argumentType = new InterfaceTypeImpl(new ClassElementImpl(identifier("B")));
    TypeVariableTypeImpl parameterType = new TypeVariableTypeImpl(new TypeVariableElementImpl(
        identifier("F")));

    InterfaceType result = type.substitute(new Type[] {argumentType}, new Type[] {parameterType});
    assertEquals(classElement, result.getElement());
    Type[] resultArguments = result.getTypeArguments();
    assertLength(1, resultArguments);
    assertEquals(parameter, resultArguments[0]);
  }
}
