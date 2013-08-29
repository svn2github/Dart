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

package com.google.dart.tools.search.internal.ui;

import com.google.common.collect.Lists;
import com.google.common.collect.Maps;
import com.google.common.collect.Sets;
import com.google.dart.engine.element.Element;
import com.google.dart.engine.element.ExecutableElement;
import com.google.dart.engine.search.MatchKind;
import com.google.dart.engine.search.SearchEngine;
import com.google.dart.engine.search.SearchMatch;
import com.google.dart.engine.source.Source;
import com.google.dart.engine.utilities.source.SourceRange;
import com.google.dart.tools.internal.corext.refactoring.util.ExecutionUtils;
import com.google.dart.tools.internal.corext.refactoring.util.RunnableEx;
import com.google.dart.tools.ui.DartPluginImages;
import com.google.dart.tools.ui.DartToolsPlugin;
import com.google.dart.tools.ui.DartUI;
import com.google.dart.tools.ui.internal.text.editor.DartEditor;
import com.google.dart.tools.ui.internal.text.editor.EditorUtility;
import com.google.dart.tools.ui.internal.text.editor.NewDartElementLabelProvider;
import com.google.dart.tools.ui.internal.util.ExceptionHandler;
import com.google.dart.tools.ui.internal.util.SWTUtil;
import com.google.dart.tools.ui.internal.viewsupport.ColoringLabelProvider;

import org.apache.commons.lang3.ArrayUtils;
import org.apache.commons.lang3.ObjectUtils;
import org.eclipse.core.resources.IFile;
import org.eclipse.core.resources.IMarker;
import org.eclipse.core.resources.IResource;
import org.eclipse.core.resources.IWorkspaceRunnable;
import org.eclipse.core.resources.ResourcesPlugin;
import org.eclipse.core.runtime.CoreException;
import org.eclipse.core.runtime.IProgressMonitor;
import org.eclipse.core.runtime.IStatus;
import org.eclipse.core.runtime.Status;
import org.eclipse.core.runtime.jobs.Job;
import org.eclipse.jface.action.Action;
import org.eclipse.jface.action.IAction;
import org.eclipse.jface.action.IMenuManager;
import org.eclipse.jface.action.IStatusLineManager;
import org.eclipse.jface.action.IToolBarManager;
import org.eclipse.jface.action.Separator;
import org.eclipse.jface.preference.IPreferenceStore;
import org.eclipse.jface.text.Position;
import org.eclipse.jface.util.IPropertyChangeListener;
import org.eclipse.jface.util.PropertyChangeEvent;
import org.eclipse.jface.viewers.DelegatingStyledCellLabelProvider;
import org.eclipse.jface.viewers.DoubleClickEvent;
import org.eclipse.jface.viewers.IBaseLabelProvider;
import org.eclipse.jface.viewers.IDoubleClickListener;
import org.eclipse.jface.viewers.ILabelProvider;
import org.eclipse.jface.viewers.ISelection;
import org.eclipse.jface.viewers.IStructuredSelection;
import org.eclipse.jface.viewers.ITreeContentProvider;
import org.eclipse.jface.viewers.StructuredSelection;
import org.eclipse.jface.viewers.StyledString;
import org.eclipse.jface.viewers.StyledString.Styler;
import org.eclipse.jface.viewers.TreeViewer;
import org.eclipse.jface.viewers.Viewer;
import org.eclipse.swt.SWT;
import org.eclipse.swt.graphics.Image;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Control;
import org.eclipse.ui.IEditorPart;
import org.eclipse.ui.progress.UIJob;

import java.nio.CharBuffer;
import java.util.Collections;
import java.util.Comparator;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.Set;

/**
 * Abstract {@link SearchPage} for displaying {@link SearchMatch}s.
 * 
 * @coverage dart.editor.ui.search
 */
public abstract class SearchMatchPage extends SearchPage {
  /**
   * Item for an element in search results tree.
   */
  private static class ElementItem {
    private final Element element;
    private final List<ElementItem> children = Lists.newArrayList();
    private final List<LineItem> lines = Lists.newArrayList();
    private ElementItem parent;
    private ElementItem prev;
    private ElementItem next;
    private int numMatches;

    public ElementItem(Element element) {
      this.element = element;
    }

    /**
     * Adds new {@link SearchMatch}, on the same or new {@link LineItem}.
     */
    public void addMatch(SourceLineProvider lineProvider, SearchMatch match) {
      ReferenceKind referenceKind = ReferenceKind.of(match.getKind());
      Source source = element.getSource();
      SourceLine sourceLine = lineProvider.getLine(source, match.getSourceRange().getOffset());
      // find target LineItem
      LineItem targetLineItem = null;
      for (LineItem lineItem : lines) {
        if (lineItem.line.equals(sourceLine)) {
          targetLineItem = lineItem;
          break;
        }
      }
      // new LineItem
      if (targetLineItem == null) {
        targetLineItem = new LineItem(this, sourceLine);
        lines.add(targetLineItem);
      }
      // prepare position
      SourceRange matchRange = match.getSourceRange();
      Position position = new Position(matchRange.getOffset(), matchRange.getLength());
      // add new position
      targetLineItem.addPosition(position, referenceKind);
    }

    @Override
    public boolean equals(Object obj) {
      if (!(obj instanceof ElementItem)) {
        return false;
      }
      return ObjectUtils.equals(((ElementItem) obj).element, element);
    }

    @Override
    public int hashCode() {
      return element != null ? element.hashCode() : 0;
    }

    public void merge(ElementItem item) {
      numMatches += item.numMatches;
      List<LineItem> thisLines = Lists.newArrayList(lines);
      for (LineItem otherLine : item.lines) {
        // try to merge into existing line
        boolean merged = false;
        for (LineItem thisLine : thisLines) {
          merged |= thisLine.merge(otherLine);
        }
        // add line
        if (!merged) {
          lines.add(otherLine);
          otherLine.item = this;
        }
      }
    }

    void addChild(ElementItem child) {
      if (child.parent == null) {
        child.parent = this;
        children.add(child);
      }
    }
  }

  /**
   * Helper for navigating {@link ElementItem} and {@link LineItem} hierarchy.
   */
  private static class ItemCursor {
    ElementItem item;
    int lineIndex;

    ItemCursor(ElementItem item) {
      this(item, -1);
    }

    ItemCursor(ElementItem item, int positionIndex) {
      this.item = item;
      this.lineIndex = positionIndex;
    }

    Position getPosition() {
      if (item == null) {
        return null;
      }
      if (lineIndex < 0 || lineIndex > item.lines.size() - 1) {
        return null;
      }
      LineItem lineItem = item.lines.get(lineIndex);
      LinePosition linePosition = lineItem.positions.get(0);
      return linePosition.position;
    }

    /**
     * Moves this {@link ItemCursor} to the next {@link LineItem} in the same or next
     * {@link ElementItem}.
     * 
     * @return {@code true} if was moved, or {@code false} if cursor is at the last line.
     */
    boolean next() {
      ElementItem _item = item;
      int _lineIndex = lineIndex;
      // try to go to next
      if (_next()) {
        return true;
      }
      // rollback
      item = _item;
      lineIndex = _lineIndex;
      return false;
    }

    /**
     * Moves this {@link ItemCursor} to the previous {@link LineItem} in the same or previous
     * {@link ElementItem}.
     * 
     * @return {@code true} if was moved, or {@code false} if cursor is at the first line.
     */
    boolean prev() {
      ElementItem _item = item;
      int _lineIndex = lineIndex;
      // try to go to previous
      if (_prev()) {
        return true;
      }
      // rollback
      item = _item;
      lineIndex = _lineIndex;
      return false;
    }

    private boolean _next() {
      if (item == null) {
        return false;
      }
      // in the same leaf
      if (lineIndex < item.lines.size() - 1) {
        lineIndex++;
        return true;
      }
      // next leaf
      while (true) {
        item = item.next;
        if (item == null) {
          return false;
        }
        if (!item.lines.isEmpty()) {
          lineIndex = 0;
          break;
        }
      }
      return true;
    }

    private boolean _prev() {
      if (item == null) {
        return false;
      }
      // in the same leaf
      if (lineIndex > 0) {
        lineIndex--;
        return true;
      }
      // previous leaf
      while (true) {
        item = item.prev;
        if (item == null) {
          return false;
        }
        if (!item.lines.isEmpty()) {
          lineIndex = item.lines.size() - 1;
          break;
        }
      }
      return true;
    }
  }

  /**
   * Item for a line with one or more matches.
   */
  private static class LineItem {
    private ElementItem item;
    private final SourceLine line;
    private final List<LinePosition> positions = Lists.newArrayList();

    public LineItem(ElementItem item, SourceLine line) {
      this.item = item;
      this.line = line;
    }

    /**
     * Adds new the {@link LinePosition} with given parameters.
     */
    public void addPosition(Position position, ReferenceKind kind) {
      positions.add(new LinePosition(position, kind));
    }

    /**
     * Attempts to merge the given {@link LineItem} into this.
     * 
     * @return {@code true} if merge was done, {@code false} if not the same line.
     */
    public boolean merge(LineItem other) {
      if (!other.line.equals(line)) {
        return false;
      }
      positions.addAll(other.positions);
      return true;
    }
  }

  /**
   * Value object with {@link Position} and its {@link ReferenceKind}.
   */
  private static class LinePosition {
    private final Position position;
    private final Position positionSrc;
    private final ReferenceKind kind;

    public LinePosition(Position position, ReferenceKind kind) {
      this.position = position;
      this.positionSrc = new Position(position.offset, position.length);
      this.kind = kind;
    }
  }

  /**
   * Coarse-grained kind of the reference. We don't need all details of {@link MatchKind}.
   */
  private static enum ReferenceKind {
    REFERENCE,
    READ,
    WRITE;
    public static ReferenceKind of(MatchKind kind) {
      if (kind == MatchKind.FIELD_READ || kind == MatchKind.VARIABLE_READ) {
        return READ;
      }
      if (kind == MatchKind.FIELD_WRITE || kind == MatchKind.VARIABLE_WRITE
          || kind == MatchKind.VARIABLE_READ_WRITE) {
        return WRITE;
      }
      return REFERENCE;
    }
  }

  /**
   * {@link ITreeContentProvider} for {@link ElementItem}s.
   */
  private static class SearchContentProvider implements ITreeContentProvider {
    @Override
    public void dispose() {
    }

    @Override
    public Object[] getChildren(Object parentElement) {
      // prepare item
      if (!(parentElement instanceof ElementItem)) {
        return ArrayUtils.EMPTY_OBJECT_ARRAY;
      }
      ElementItem item = (ElementItem) parentElement;
      // sub-items
      List<ElementItem> children = item.children;
      if (!children.isEmpty()) {
        return children.toArray(new ElementItem[children.size()]);
      }
      // lines
      List<LineItem> lines = item.lines;
      return lines.toArray(new LineItem[lines.size()]);
    }

    @Override
    public Object[] getElements(Object inputElement) {
      return getChildren(inputElement);
    }

    @Override
    public Object getParent(Object element) {
      if (element instanceof ElementItem) {
        return ((ElementItem) element).parent;
      }
      if (element instanceof LineItem) {
        return ((LineItem) element).item;
      }
      return null;
    }

    @Override
    public boolean hasChildren(Object element) {
      if (element instanceof ElementItem) {
        ElementItem item = (ElementItem) element;
        return !item.children.isEmpty() || !item.lines.isEmpty();
      }
      return false;
    }

    @Override
    public void inputChanged(Viewer viewer, Object oldInput, Object newInput) {
    }
  }

  /**
   * {@link ILabelProvider} for {@link ElementItem}s.
   */
  private static class SearchLabelProvider extends NewDartElementLabelProvider {
    @Override
    public Image getImage(Object elem) {
      // element
      if (elem instanceof ElementItem) {
        ElementItem item = (ElementItem) elem;
        return super.getImage(item.element);
      }
      // line
      if (elem instanceof LineItem) {
        LineItem item = (LineItem) elem;
        // has any write?
        for (LinePosition position : item.positions) {
          if (position.kind == ReferenceKind.WRITE) {
            return DartPluginImages.get(DartPluginImages.IMG_OBJS_SEARCH_WRITEACCESS);
          }
        }
        // has any read?
        for (LinePosition position : item.positions) {
          if (position.kind == ReferenceKind.READ) {
            return DartPluginImages.get(DartPluginImages.IMG_OBJS_SEARCH_READACCESS);
          }
        }
        // just some reference
        return DartPluginImages.get(DartPluginImages.IMG_OBJS_SEARCH_OCCURRENCE);
      }
      // unknown
      return null;
    }

    @Override
    public StyledString getStyledText(Object elem) {
      if (elem instanceof ElementItem) {
        ElementItem item = (ElementItem) elem;
        StyledString styledText = super.getStyledText(item.element);
        if (item.numMatches == 1) {
          styledText.append(" (1 match)", StyledString.COUNTER_STYLER);
        } else if (item.numMatches > 1) {
          styledText.append(" (" + item.numMatches + " matches)", StyledString.COUNTER_STYLER);
        }
        return styledText;
      } else {
        LineItem item = (LineItem) elem;
        StyledString styledText = new StyledString(item.line.content);
        for (LinePosition linePosition : item.positions) {
          Styler style = linePosition.kind == ReferenceKind.WRITE
              ? ColoringLabelProvider.HIGHLIGHT_WRITE_STYLE : ColoringLabelProvider.HIGHLIGHT_STYLE;
          int styleOffset = linePosition.positionSrc.offset - item.line.start;
          int styleLength = linePosition.positionSrc.length;
          styledText.setStyle(styleOffset, styleLength, style);
        }
        return styledText;
      }
    }
  }

  /**
   * Information about a single line in some {@link Source}.
   */
  private static class SourceLine {
    final Source source;
    final int start;
    final String content;

    public SourceLine(Source source, int start, String content) {
      this.source = source;
      this.start = start;
      this.content = content;
    }

    @Override
    public boolean equals(Object obj) {
      if (!(obj instanceof SourceLine)) {
        return false;
      }
      SourceLine other = (SourceLine) obj;
      return other.source == source && other.start == start;
    }
  }

  /**
   * Helper for transforming offsets in the some {@link Source} into {@link SourceLine} objects.
   */
  private static class SourceLineProvider {
    /**
     * @return the {@link String} content of the given {@link Source}.
     */
    private static String getSourceContent(Source source) throws Exception {
      final String result[] = {null};
      source.getContents(new Source.ContentReceiver() {
        @Override
        public void accept(CharBuffer contents, long modificationTime) {
          result[0] = contents.toString();
        }

        @Override
        public void accept(String contents, long modificationTime) {
          result[0] = contents;
        }
      });
      return result[0];
    }

    private final Map<Source, String> sourceContentMap = Maps.newHashMap();

    /**
     * @return the {@link SourceLine} for the given {@link Source} and offset; may be {@code null}.
     */
    public SourceLine getLine(Source source, int offset) {
      String content = getContent(source);
      // find start of line
      int start = offset;
      while (start > 0 && content.charAt(start - 1) != '\n') {
        start--;
      }
      // find end of line
      int end = offset;
      while (end < content.length() && content.charAt(end) != '\r' && content.charAt(end) != '\n') {
        end++;
      }
      // done
      String text = content.substring(start, end);
      return new SourceLine(source, start, text);
    }

    private String getContent(Source source) {
      String content = sourceContentMap.get(source);
      if (content == null) {
        try {
          content = getSourceContent(source);
          sourceContentMap.put(source, content);
        } catch (Throwable e) {
          return null;
        }
      }
      return content;
    }
  }

  private static final ITreeContentProvider CONTENT_PROVIDER = new SearchContentProvider();
  private static final IBaseLabelProvider LABEL_PROVIDER = new DelegatingStyledCellLabelProvider(
      new SearchLabelProvider());

  /**
   * Adds new {@link ElementItem} to the tree.
   */
  private static ElementItem addElementItem(Map<Element, ElementItem> itemMap, ElementItem child) {
    // put child
    Element childElement = child.element;
    {
      ElementItem existingChild = itemMap.get(childElement);
      if (existingChild == null) {
        itemMap.put(childElement, child);
      } else {
        existingChild.merge(child);
        child = existingChild;
      }
    }
    // bind child to parent
    if (childElement != null) {
      Element parentElement = childElement.getEnclosingElement();
      ElementItem parent = new ElementItem(parentElement);
      parent = addElementItem(itemMap, parent);
      parent.addChild(child);
    }
    // done
    return child;
  }

  /**
   * Builds {@link ElementItem} tree out of the given {@link SearchMatch}s.
   */
  private static ElementItem buildElementItemTree(List<SearchMatch> matches) {
    SourceLineProvider sourceLineProvider = new SourceLineProvider();
    ElementItem rootItem = new ElementItem(null);
    Map<Element, ElementItem> itemMap = Maps.newHashMap();
    itemMap.put(null, rootItem);
    for (SearchMatch match : matches) {
      Element element = getExecutableElement(match);
      ElementItem elementItem = new ElementItem(element);
      elementItem.addMatch(sourceLineProvider, match);
      addElementItem(itemMap, elementItem);
    }
    calculateNumMatches(rootItem);
    sortLines(rootItem);
    linkLeaves(rootItem, null);
    return rootItem;
  }

  /**
   * Recursively calculates {@link ElementItem#numMatches} fields.
   */
  private static int calculateNumMatches(ElementItem item) {
    int result = 0;
    for (LineItem lineItem : item.lines) {
      result += lineItem.positions.size();
    }
    for (ElementItem child : item.children) {
      result += calculateNumMatches(child);
    }
    item.numMatches = result;
    return result;
  }

  /**
   * @return the {@link Element} to use as enclosing in {@link ElementItem} tree.
   */
  private static Element getExecutableElement(SearchMatch match) {
    Element element = match.getElement();
    while (element != null) {
      Element executable = element.getAncestor(ExecutableElement.class);
      if (executable == null) {
        break;
      }
      element = executable;
    }
    return element;
  }

  /**
   * Recursively visits {@link ElementItem} and links leaves.
   * 
   * @return the last {@link ElementItem} leaf in the sub-tree.
   */
  private static ElementItem linkLeaves(ElementItem item, ElementItem prev) {
    // leaf
    if (item.children.isEmpty()) {
      if (prev != null) {
        prev.next = item;
      }
      item.prev = prev;
      prev = item;
      return item;
    }
    // container
    ElementItem lastLeaf = prev;
    item.next = item.children.get(0);
    for (ElementItem child : item.children) {
      lastLeaf = linkLeaves(child, lastLeaf);
    }
    return lastLeaf;
  }

  /**
   * Opens the {@link Position} in the {@link Element}s editor.
   */
  private static void openPositionInElement(Element element, Position position) {
    try {
      IEditorPart editor = DartUI.openInEditor(element);
      revealInEditor(editor, position);
    } catch (Throwable e) {
      ExceptionHandler.handle(e, "Search", "Exception during open.");
    }
  }

  /**
   * Reveals the given {@link Position} in the {@link IEditorPart}.
   */
  private static void revealInEditor(IEditorPart editor, Position position) {
    SourceRange sourceRange = new SourceRange(position.offset, position.length);
    EditorUtility.revealInEditor(editor, sourceRange);
  }

  /**
   * Recursively sorts {@link ElementItem}s and {@link LineItem}s.
   */
  private static void sortLines(ElementItem item) {
    // process lines
    Collections.sort(item.lines, new Comparator<LineItem>() {
      @Override
      public int compare(LineItem o1, LineItem o2) {
        return o1.line.start - o2.line.start;
      }
    });
    for (LineItem lineItem : item.lines) {
      Collections.sort(lineItem.positions, new Comparator<LinePosition>() {
        @Override
        public int compare(LinePosition o1, LinePosition o2) {
          return o1.position.offset - o2.position.offset;
        }
      });
    }
    // process children
    Collections.sort(item.children, new Comparator<ElementItem>() {
      @Override
      public int compare(ElementItem o1, ElementItem o2) {
        return o1.element.getNameOffset() - o2.element.getNameOffset();
      }
    });
    for (ElementItem child : item.children) {
      sortLines(child);
    }
  }

  private IAction removeAction = new Action() {
    {
      setToolTipText("Remove Selected Matches");
      DartPluginImages.setLocalImageDescriptors(this, "search_rem.gif");
    }

    @Override
    public void run() {
      IStructuredSelection selection = (IStructuredSelection) viewer.getSelection();
      for (Iterator<?> iter = selection.toList().iterator(); iter.hasNext();) {
        Object obj = iter.next();
        // LineItem
        if (obj instanceof LineItem) {
          LineItem lineItem = (LineItem) obj;
          ElementItem parentItem = lineItem.item;
          List<LineItem> parentLines = parentItem.lines;
          // remove this line
          parentLines.remove(lineItem);
          // it no more lines, remove parent item too
          if (parentLines.isEmpty()) {
            obj = parentItem;
          }
        }
        // ResultItem
        if (obj instanceof ElementItem) {
          ElementItem item = (ElementItem) obj;
          while (item != null && item.element != null) {
            ElementItem parent = item.parent;
            parent.children.remove(item);
            if (!parent.children.isEmpty()) {
              break;
            }
            item = parent;
          }
        }
      }
      calculateNumMatches(rootItem);
      // update viewer
      viewer.refresh();
      // update markers
      addMarkers();
    }
  };

  private IAction removeAllAction = new Action() {
    {
      setToolTipText("Remove All Matches");
      DartPluginImages.setLocalImageDescriptors(this, "search_remall.gif");
    }

    @Override
    public void run() {
      searchView.showPage(null);
    }
  };

  private IAction refreshAction = new Action() {
    {
      setToolTipText("Refresh the Current Search");
      DartPluginImages.setLocalImageDescriptors(this, "refresh.gif");
    }

    @Override
    public void run() {
      refresh();
    }
  };

  private IAction expandAllAction = new Action() {
    {
      setToolTipText("Expand All");
      DartPluginImages.setLocalImageDescriptors(this, "expandall.gif");
    }

    @Override
    public void run() {
      viewer.expandAll();
    }
  };

  private IAction collapseAllAction = new Action() {
    {
      setToolTipText("Collapse All");
      DartPluginImages.setLocalImageDescriptors(this, "collapseall.gif");
    }

    @Override
    public void run() {
      viewer.collapseAll();
    }
  };

  private IAction nextAction = new Action() {
    {
      setToolTipText("Show Next Match");
      DartPluginImages.setLocalImageDescriptors(this, "search_next.gif");
    }

    @Override
    public void run() {
      openItemNext();
    }
  };
  private IAction prevAction = new Action() {
    {
      setToolTipText("Show Previous Match");
      DartPluginImages.setLocalImageDescriptors(this, "search_prev.gif");
    }

    @Override
    public void run() {
      openItemPrev();
    }
  };
  private final SearchView searchView;
  private final String taskName;
  private final Set<IResource> markerResources = Sets.newHashSet();

  private TreeViewer viewer;

  private IPreferenceStore preferences;
  private IPropertyChangeListener propertyChangeListener = new IPropertyChangeListener() {
    @Override
    public void propertyChange(PropertyChangeEvent event) {
      updateColors();
    }
  };

  private ElementItem rootItem;
  private ItemCursor itemCursor;
  private PositionTracker positionTracker;

  private long lastQueryStartTime = 0;
  private long lastQueryFinishTime = 0;

  public SearchMatchPage(SearchView searchView, String taskName) {
    this.searchView = searchView;
    this.taskName = taskName;
  }

  @Override
  public void createControl(Composite parent) {
    viewer = new TreeViewer(parent, SWT.FULL_SELECTION);
    viewer.setContentProvider(CONTENT_PROVIDER);
    viewer.setLabelProvider(LABEL_PROVIDER);
    viewer.addDoubleClickListener(new IDoubleClickListener() {
      @Override
      public void doubleClick(DoubleClickEvent event) {
        ISelection selection = event.getSelection();
        openSelectedElement(selection);
      }
    });
    // update colors
    preferences = DartToolsPlugin.getDefault().getCombinedPreferenceStore();
    preferences.addPropertyChangeListener(propertyChangeListener);
    updateColors();
    SWTUtil.bindJFaceResourcesFontToControl(viewer.getControl());
  }

  @Override
  public void dispose() {
    preferences.removePropertyChangeListener(propertyChangeListener);
    removeMarkers();
    disposePositionTracker();
    super.dispose();
  }

  @Override
  public Control getControl() {
    return viewer.getControl();
  }

  @Override
  public long getLastQueryFinishTime() {
    return lastQueryFinishTime;
  }

  @Override
  public long getLastQueryStartTime() {
    return lastQueryStartTime;
  }

  @Override
  public void makeContributions(IMenuManager menuManager, IToolBarManager toolBarManager,
      IStatusLineManager statusLineManager) {
    toolBarManager.add(nextAction);
    toolBarManager.add(prevAction);
    toolBarManager.add(new Separator());
    toolBarManager.add(removeAction);
    toolBarManager.add(removeAllAction);
    toolBarManager.add(new Separator());
    toolBarManager.add(expandAllAction);
    toolBarManager.add(collapseAllAction);
    toolBarManager.add(new Separator());
    toolBarManager.add(refreshAction);
  }

  @Override
  public void setFocus() {
    viewer.getControl().setFocus();
  }

  @Override
  public void show() {
    refresh();
  }

  /**
   * @return the description of given search results.
   */
  protected abstract String getPostQueryDescription(List<SearchMatch> matches);

  /**
   * Runs a {@link SearchEngine} request.
   * 
   * @return the {@link SearchMatch}s to display.
   */
  protected abstract List<SearchMatch> runQuery();

  /**
   * Adds markers for all {@link ElementItem}s starting from {@link #rootItem}.
   */
  private void addMarkers() {
    try {
      ResourcesPlugin.getWorkspace().run(new IWorkspaceRunnable() {
        @Override
        public void run(IProgressMonitor monitor) throws CoreException {
          removeMarkers();
          markerResources.clear();
          addMarkers(rootItem);
        }
      }, null);
    } catch (Throwable e) {
      DartToolsPlugin.log(e);
    }
  }

  /**
   * Adds markers for the given {@link ElementItem} and its children.
   */
  private void addMarkers(ElementItem item) throws CoreException {
    // add marker if leaf
    if (!item.lines.isEmpty()) {
      IFile resource = DartUI.getElementFile(item.element);
      if (resource != null && resource.exists()) {
        markerResources.add(resource);
        try {
          for (LineItem lineItem : item.lines) {
            List<LinePosition> positions = lineItem.positions;
            for (LinePosition linePosition : positions) {
              Position position = linePosition.position;
              IMarker marker = resource.createMarker(SearchView.SEARCH_MARKER);
              marker.setAttribute(IMarker.CHAR_START, position.getOffset());
              marker.setAttribute(IMarker.CHAR_END, position.getOffset() + position.getLength());
            }
          }
        } catch (Throwable e) {
        }
      }
    }
    // process children
    for (ElementItem child : item.children) {
      addMarkers(child);
    }
  }

  /**
   * Disposes {@link #positionTracker}.
   */
  private void disposePositionTracker() {
    if (positionTracker == null) {
      return;
    }
    positionTracker.dispose();
    positionTracker = null;
  }

  /**
   * Analyzes each {@link ElementItem} and expends it in {@link #viewer} only if it has not too much
   * children. So, user will see enough information, but not too much.
   */
  private void expandWhileSmallNumberOfChildren(List<ElementItem> items) {
    for (ElementItem item : items) {
      if (item.children.size() <= 5) {
        viewer.setExpandedState(item, true);
        expandWhileSmallNumberOfChildren(item.children);
      }
    }
  }

  /**
   * Opens {@link DartEditor} with the next {@link Position} in the same of the next
   * {@link ElementItem}.
   */
  private void openItemNext() {
    boolean changed = itemCursor.next();
    if (changed) {
      showCursor();
    }
  }

  /**
   * Opens {@link DartEditor} with the previous {@link Position} in the same of the previous
   * {@link ElementItem}.
   */
  private void openItemPrev() {
    boolean changed = itemCursor.prev();
    if (changed) {
      showCursor();
    }
  }

  /**
   * Opens selected {@link ElementItem} in the editor.
   */
  private void openSelectedElement(ISelection selection) {
    // need IStructuredSelection
    if (!(selection instanceof IStructuredSelection)) {
      return;
    }
    IStructuredSelection structuredSelection = (IStructuredSelection) selection;
    // only single element
    if (structuredSelection.size() != 1) {
      return;
    }
    Object firstElement = structuredSelection.getFirstElement();
    // line item
    if (firstElement instanceof LineItem) {
      LineItem lineItem = (LineItem) firstElement;
      Position position = lineItem.positions.get(0).position;
      openPositionInElement(lineItem.item.element, position);
    }
    // element item
    if (firstElement instanceof ElementItem) {
      ElementItem item = (ElementItem) firstElement;
      // use ResultCursor to find first occurrence in the requested subtree
      itemCursor = new ItemCursor(item);
      boolean found = itemCursor.next();
      if (!found) {
        return;
      }
      Element element = itemCursor.item.element;
      Position position = itemCursor.getPosition();
      // open position
      openPositionInElement(element, position);
    }
  }

  /**
   * Runs background {@link Job} to fetch {@link SearchMatch}s and then displays them in the
   * {@link #viewer}.
   */
  private void refresh() {
    try {
      lastQueryStartTime = System.currentTimeMillis();
      new Job(taskName) {
        @Override
        protected IStatus run(IProgressMonitor monitor) {
          // do query
          List<SearchMatch> matches = runQuery();
          setContentDescription(getPostQueryDescription(matches));
          // process query results
          rootItem = buildElementItemTree(matches);
          itemCursor = new ItemCursor(rootItem);
          trackPositions();
          // add markers
          addMarkers();
          // schedule UI update
          new UIJob("Displaying search results...") {
            @Override
            public IStatus runInUIThread(IProgressMonitor monitor) {
              // may be already disposed (e.g. new search was done)
              if (viewer.getControl().isDisposed()) {
                return Status.OK_STATUS;
              }
              // set new input
              Object[] expandedElements = viewer.getExpandedElements();
              viewer.setInput(rootItem);
              viewer.setExpandedElements(expandedElements);
              // expand
              expandWhileSmallNumberOfChildren(rootItem.children);
              lastQueryFinishTime = System.currentTimeMillis();
              return Status.OK_STATUS;
            }
          }.schedule();
          // done
          return Status.OK_STATUS;
        }
      }.schedule();
    } catch (Throwable e) {
      ExceptionHandler.handle(e, "Search", "Exception during search.");
    }
  }

  /**
   * Removes all search markers from {@link #markerResources}.
   */
  private void removeMarkers() {
    try {
      ResourcesPlugin.getWorkspace().run(new IWorkspaceRunnable() {
        @Override
        public void run(IProgressMonitor monitor) throws CoreException {
          for (IResource resource : markerResources) {
            if (resource.exists()) {
              try {
                resource.deleteMarkers(SearchView.SEARCH_MARKER, false, IResource.DEPTH_ZERO);
              } catch (Throwable e) {
              }
            }
          }
        }
      }, null);
    } catch (Throwable e) {
      DartToolsPlugin.log(e);
    }
  }

  /**
   * Shows given text as description for {@link SearchView}.
   */
  private void setContentDescription(final String description) {
    ExecutionUtils.runRethrowUI(new RunnableEx() {
      @Override
      public void run() throws Exception {
        searchView.setContentDescription(description);
      }
    });
  }

  /**
   * Shows current {@link #itemCursor} state.
   */
  private void showCursor() {
    try {
      ElementItem elementItem = itemCursor.item;
      LineItem lineItem = elementItem.lines.get(itemCursor.lineIndex);
      viewer.setSelection(new StructuredSelection(lineItem), true);
      // open editor with Element
      Element element = elementItem.element;
      IEditorPart editor = DartUI.openInEditor(element, false, true);
      // show Position
      Position position = itemCursor.getPosition();
      if (position != null) {
        revealInEditor(editor, position);
      }
    } catch (Throwable e) {
      ExceptionHandler.handle(e, "Search", "Exception during open.");
    }
  }

  /**
   * Starts tracking all search result positions in {@link #positionTracker}.
   */
  private void trackPositions() {
    disposePositionTracker();
    positionTracker = new PositionTracker();
    trackPositions(rootItem);
  }

  /**
   * Recursively visits {@link ElementItem} and tracks all {@link Position}s.
   */
  private void trackPositions(ElementItem item) {
    // do track positions
    if (item.element != null) {
      IFile file = DartUI.getElementFile(item.element);
      if (file != null) {
        for (LineItem lineItem : item.lines) {
          List<LinePosition> positions = lineItem.positions;
          for (LinePosition linePosition : positions) {
            Position position = linePosition.position;
            positionTracker.trackPosition(file, position);
          }
        }
      }
    }
    // process children
    for (ElementItem child : item.children) {
      trackPositions(child);
    }
  }

  private void updateColors() {
    if (viewer.getTree().isDisposed()) {
      return;
    }
    SWTUtil.setColors(viewer.getTree(), preferences);
  }
}
