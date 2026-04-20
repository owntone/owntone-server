import { onUnmounted, reactive } from 'vue'

const useScroll = () => {
  const SCROLL_ZONE = 80
  const SCROLL_SPEED = 10
  let scrollFrame = null

  const stop = () => {
    if (scrollFrame) {
      cancelAnimationFrame(scrollFrame)
      scrollFrame = null
    }
  }

  const start = (container, direction) => {
    stop()
    const step = () => {
      if (container === window) {
        window.scrollBy(0, direction * SCROLL_SPEED)
      } else {
        container.scrollTop += direction * SCROLL_SPEED
      }
      scrollFrame = requestAnimationFrame(step)
    }
    scrollFrame = requestAnimationFrame(step)
  }

  const getScrollContainer = (element) => {
    let el = element
    while (el && el !== document.body) {
      const { overflowY } = window.getComputedStyle(el)
      if (overflowY === 'auto' || overflowY === 'scroll') {
        return el
      }
      el = el.parentElement
    }
    return window
  }

  const handle = (clientY, target) => {
    const container = getScrollContainer(target)
    const rect =
      (container === window && {
        top: 0,
        bottom: window.innerHeight
      }) ||
      container.getBoundingClientRect()

    if (clientY < rect.top + SCROLL_ZONE) {
      start(container, -1)
    } else if (clientY > rect.bottom - SCROLL_ZONE) {
      start(container, 1)
    } else {
      stop()
    }
  }

  return { handle, stop }
}

const getIndexFromPoint = (clientX, clientY) => {
  const target = document.elementFromPoint(clientX, clientY)
  const item = target?.closest('[data-drag-index]')
  if (item) {
    return Number(item.dataset.dragIndex)
  }
  return null
}

const endMove = ({ dragState, onMoveEnd }) => {
  const { from, to } = dragState
  if (from !== null && from !== to) {
    onMoveEnd({ from, to: to - (from < to) })
  }
  dragState.from = null
  dragState.to = null
}

const useMouseDrag = ({ dragState, onMoveEnd, scroll }) => {
  const onDocumentDragOver = (event) => {
    const item = event.target?.closest('[data-drag-index]')
    if (!item) {
      scroll.stop()
    }
  }

  const cleanup = () => {
    scroll.stop()
    document.removeEventListener('dragover', onDocumentDragOver)
  }

  const onDragStart = (index) => {
    dragState.from = index
    document.addEventListener('dragover', onDocumentDragOver)
    document.addEventListener('dragend', cleanup, { once: true })
  }

  const onDragOver = (event, index) => {
    event.preventDefault()
    dragState.to = index
    scroll.handle(event.clientY, event.target)
  }

  const onDrop = () => {
    cleanup()
    endMove({ dragState, onMoveEnd })
  }

  return { onDragStart, onDragOver, onDrop }
}

const useTouchDrag = ({ dragState, onMoveEnd, scroll }) => {
  let dragFromHandle = false

  const onTouchStart = (event) => {
    dragFromHandle = Boolean(event.target.closest('[data-drag-handle]'))
    if (dragFromHandle) {
      const item = event.currentTarget
      dragState.from = parseInt(item.dataset.dragIndex, 10)
    }
  }

  const onTouchMove = (event) => {
    if (dragFromHandle) {
      event.preventDefault()
      const [{ clientX, clientY }] = event.touches
      const index = getIndexFromPoint(clientX, clientY)
      if (index !== null) {
        dragState.to = index
      }
      scroll.handle(clientY, event.target)
    }
  }

  const onTouchEnd = () => {
    if (dragFromHandle) {
      scroll.stop()
      endMove({ dragState, onMoveEnd })
    }
    dragFromHandle = false
  }

  return { onTouchStart, onTouchMove, onTouchEnd }
}

const useCleanup = (clearDragState) => {
  document.addEventListener('visibilitychange', clearDragState)
  window.addEventListener('blur', clearDragState)

  onUnmounted(() => {
    document.removeEventListener('visibilitychange', clearDragState)
    window.removeEventListener('blur', clearDragState)
  })
}

export const useDraggableList = (onMoveEnd) => {
  const dragState = reactive({ from: null, to: null })
  const scroll = useScroll()

  const isDragged = (index) => dragState.from === index
  const isDraggedOver = (index) => dragState.to === index

  const { onDragStart, onDragOver, onDrop } = useMouseDrag({
    dragState,
    onMoveEnd,
    scroll
  })
  const { onTouchStart, onTouchMove, onTouchEnd } = useTouchDrag({
    dragState,
    onMoveEnd,
    scroll
  })

  const clearDragState = () => {
    scroll.stop()
    dragState.to = null
    dragState.from = null
  }

  useCleanup(clearDragState)

  return {
    isDragged,
    isDraggedOver,
    onDragStart,
    onDragOver,
    onDrop,
    onTouchStart,
    onTouchMove,
    onTouchEnd
  }
}
