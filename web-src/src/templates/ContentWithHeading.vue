<template>
  <section class="section fd-content">
    <div class="container">
      <div class="columns is-centered">
        <div class="column is-four-fifths">
          <slot name="options"></slot>
          <nav class="level" id="top">
            <!-- Left side -->
            <div class="level-left">
              <div class="level-item has-text-centered-mobile">
                <div>
                  <slot name="heading-left"></slot>
                </div>
              </div>
            </div>

            <!-- Right side -->
            <div class="level-right has-text-centered-mobile">
              <slot name="heading-right"></slot>
            </div>
          </nav>
          <slot name="content"></slot>
          <div style="margin-top: 16px;">
            <slot name="footer"></slot>
          </div>
        </div>
      </div>
    </div>
  </section>
</template>

<script>
</script>

<style>
</style>
