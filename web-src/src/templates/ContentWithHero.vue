<template>
  <div>
    <section class="hero is-light is-bold fd-content">
      <div class="hero-body">
        <div class="container">
          <div class="columns is-centered">
            <div class="column is-four-fifths">
              <div class="columns" style="flex-direction: row-reverse;">
                <div class="column fd-has-cover">
                  <!-- Slot heading right -->
                  <slot name="heading-right"></slot>
                </div>
                <div class="column is-three-fifths has-text-centered-mobile" style="margin: auto 0;">
                    <!-- Slot heading left -->
                    <slot name="heading-left"></slot>
                </div>
              </div>
            </div>
          </div>
        </div>
      </div>
    </section>
    <section class="section fd-content">
      <div class="container">
        <div class="columns is-centered">
          <div class="column is-four-fifths">
            <!-- Slot content -->
            <slot name="content"></slot>
            <div style="margin-top: 16px;">
              <!-- Slot footer -->
              <slot name="footer"></slot>
            </div>
          </div>
        </div>
      </div>
    </section>
  </div>
</template>

<script>
</script>

<style>
</style>
