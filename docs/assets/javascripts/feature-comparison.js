// SPDX-FileCopyrightText: 2026 The ng-log contributors
// SPDX-License-Identifier: BSD-3-Clause

(function () {
  'use strict'

  const statusOrder = new Map([
    ['✅', 0],
    ['⚠️', 1],
    ['❌', 2],
  ])

  function statusRank(cell) {
    for (const [mark, rank] of statusOrder) {
      if (cell.includes(mark)) {
        return rank
      }
    }
    return statusOrder.size
  }

  function sortTable(table, column, ascending) {
    const body = table.tBodies[0]
    if (!body) {
      return
    }

    const rows = Array.from(body.rows)
    rows.sort((left, right) => {
      const leftCell = left.cells[column]
      const rightCell = right.cells[column]
      if (!leftCell || !rightCell) {
        return 0
      }

      let result
      if (column === 0) {
        result = leftCell.textContent.trim().localeCompare(
          rightCell.textContent.trim(),
        )
      } else {
        result = statusRank(leftCell.textContent) -
          statusRank(rightCell.textContent)
        if (result === 0) {
          result = left.cells[0].textContent.trim().localeCompare(
            right.cells[0].textContent.trim(),
          )
        }
      }
      return ascending ? result : -result
    })

    for (const row of rows) {
      body.append(row)
    }
  }

  function updateSortButtons(headers, activeColumn, ascending) {
    headers.forEach((header, column) => {
      const button = header.querySelector('button')
      if (!button) {
        return
      }

      const active = column === activeColumn
      const arrow = active ? (ascending ? ' ⬆' : ' ⬇') : ' ⬍'
      button.textContent = button.dataset.label + arrow
      header.setAttribute(
        'aria-sort',
        active ? (ascending ? 'ascending' : 'descending') : 'none',
      )
    })
  }

  function initializeFeatureMatrix() {
    const namedElement = document.getElementById('feature-matrix')
    const table = namedElement?.tagName === 'TABLE' ? namedElement :
      Array.from(document.querySelectorAll('table')).find((candidate) => {
        const firstHeader = candidate.tHead?.rows[0]?.cells[0]
        return firstHeader?.textContent.trim() === 'Feature'
      })
    if (!table || table.dataset.sortable === 'true' || !table.tHead) {
      return
    }

    const headers = Array.from(table.tHead.rows[0].cells)
    let activeColumn = -1
    let ascending = true

    headers.forEach((header, column) => {
      const label = header.textContent.trim()
      const button = document.createElement('button')
      button.type = 'button'
      button.dataset.label = label
      button.title = `Sort by ${label}`
      header.replaceChildren(button)
      button.addEventListener('click', () => {
        if (activeColumn === column) {
          ascending = !ascending
        } else {
          activeColumn = column
          ascending = true
        }
        sortTable(table, activeColumn, ascending)
        updateSortButtons(headers, activeColumn, ascending)
      })
    })

    table.dataset.sortable = 'true'
    updateSortButtons(headers, activeColumn, ascending)
  }

  initializeFeatureMatrix()

  if (typeof document$ !== 'undefined') {
    document$.subscribe(initializeFeatureMatrix)
  }

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', initializeFeatureMatrix)
  } else {
    initializeFeatureMatrix()
  }

  window.addEventListener('load', initializeFeatureMatrix)
})()
