<?php

// This doc comment block generated by idl/sysdoc.php
/**
 * ( excerpt from
 * http://php.net/manual/en/class.recursivefilteriterator.php )
 *
 * This abstract iterator filters out unwanted values for a
 * RecursiveIterator. This class should be extended to implement custom
 * filters. The RecursiveFilterIterator::accept() must be implemented in
 * the subclass.
 *
 */
abstract class RecursiveFilterIterator extends FilterIterator
  implements OuterIterator, RecursiveIterator {

  // This doc comment block generated by idl/sysdoc.php
  /**
   * ( excerpt from
   * http://php.net/manual/en/recursivefilteriterator.construct.php )
   *
   * Create a RecursiveFilterIterator from a RecursiveIterator.
   *
   * @iterator   mixed   The RecursiveIterator to be filtered.
   *
   * @return     mixed   No value is returned.
   */
  public function __construct (RecursiveIterator $iterator) {
    return parent::__construct($iterator);
  }

  // This doc comment block generated by idl/sysdoc.php
  /**
   * ( excerpt from
   * http://php.net/manual/en/recursivefilteriterator.getchildren.php )
   *
   * Return the inner iterator's children contained in a
   * RecursiveFilterIterator.
   *
   * @return     mixed   Returns a RecursiveFilterIterator containing the
   *                     inner iterator's children.
   */
  public function getChildren() {
    return new static($this->getInnerIterator()->getChildren());
  }

  // This doc comment block generated by idl/sysdoc.php
  /**
   * ( excerpt from
   * http://php.net/manual/en/recursivefilteriterator.haschildren.php )
   *
   * Check whether the inner iterator's current element has children.
   *
   * @return     mixed   TRUE if the inner iterator has children, otherwise
   *                     FALSE
   */
  public function hasChildren() {
    return $this->getInnerIterator()->hasChildren();
  }

}