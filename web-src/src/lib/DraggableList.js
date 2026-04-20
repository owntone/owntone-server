import { ref } from 'vue'

const useAutoScroll = () => {
  const SCROLL_ZONE = 80
  const SCROLL_SPEED = 8
  let scrollFrame = null

  const stopScroll = () => {
    if (scrollFrame) {
      cancelAnimationFrame(scrollFrame)
      scrollFrame = null
    }
  }

  const startScroll = (container, direction) => {
    stopScroll()
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

  const handleAutoScroll = (clientY, target) => {
    const container = getScrollContainer(target)
    const rect =
      (container === window && {
        top: 0,
        bottom: window.innerHeight
      }) ||
      container.getBoundingClientRect()
    if (clientY < rect.top + SCROLL_ZONE) {
      startScroll(container, -1)
    } else if (clientY > rect.bottom - SCROLL_ZONE) {
      startScroll(container, 1)
    } else {
      stopScroll()
    }
  }

  return { handleAutoScroll, stopScroll }
}

const getIndexFromPoint = (clientX, clientY) => {
  const target = document.elementFromPoint(clientX, clientY)
  const item = target.closest('[data-drag-index]')
  return Number(item.dataset.dragIndex)
}

const endMove = ({ draggedIndex, dragOverIndex, onMoveEnd }) => {
  const from = draggedIndex.value
  const to = dragOverIndex.value
  if (from !== null && from !== to) {
    onMoveEnd({ from, to: to - (from < to) })
  }
  draggedIndex.value = null
  dragOverIndex.value = null
}

const useMouseDrag = ({
  draggedIndex,
  dragOverIndex,
  onMoveEnd,
  handleAutoScroll,
  stopScroll
}) => {
  const onDragStart = (index) => {
    draggedIndex.value = index
  }

  const onDragOver = (event, index) => {
    event.preventDefault()
    dragOverIndex.value = index
    handleAutoScroll(event.clientY, event.target)
  }

  const onDrop = () => {
    stopScroll()
    endMove({ draggedIndex, dragOverIndex, onMoveEnd })
  }

  return { onDragStart, onDragOver, onDrop }
}

const useTouchDrag = ({
  draggedIndex,
  dragOverIndex,
  onMoveEnd,
  handleAutoScroll,
  stopScroll
}) => {
  let dragFromHandle = false

  const onTouchStart = (event) => {
    dragFromHandle = Boolean(event.target.closest('[data-drag-handle]'))
    if (dragFromHandle) {
      const item = event.currentTarget
      draggedIndex.value = parseInt(item.dataset.dragIndex, 10)
    }
  }

  const onTouchMove = (event) => {
    if (dragFromHandle) {
      event.preventDefault()
      const [{ clientX, clientY }] = event.touches
      const index = getIndexFromPoint(clientX, clientY)
      if (index !== null) {
        dragOverIndex.value = index
      }
      handleAutoScroll(clientY, event.target)
    }
  }

  const onTouchEnd = () => {
    if (dragFromHandle) {
      stopScroll()
      endMove({ draggedIndex, dragOverIndex, onMoveEnd })
    }
    dragFromHandle = false
  }

  return { onTouchStart, onTouchMove, onTouchEnd }
}

export const useDraggableList = (onMoveEnd) => {
  const draggedIndex = ref(null)
  const dragOverIndex = ref(null)
  const { handleAutoScroll, stopScroll } = useAutoScroll()

  const isDragged = (index) => draggedIndex.value === index

  const isDraggedOver = (index) => dragOverIndex.value === index

  const { onDragStart, onDragOver, onDrop } = useMouseDrag({
    draggedIndex,
    dragOverIndex,
    onMoveEnd,
    handleAutoScroll,
    stopScroll
  })

  const { onTouchStart, onTouchMove, onTouchEnd } = useTouchDrag({
    draggedIndex,
    dragOverIndex,
    onMoveEnd,
    handleAutoScroll,
    stopScroll
  })

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
