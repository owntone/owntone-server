<template>
  <section class="hero is-light is-bold">
    <div class="hero-body">
      <div class="container">
        <div class="columns is-centered">
          <div class="column is-four-fifths">
            <div class="columns is-flex-direction-row-reverse">
              <div class="column fd-has-cover">
                <!-- Slot heading right -->
                <slot name="heading-right" />
              </div>
              <div
                class="column m-auto is-three-fifths has-text-centered-mobile"
              >
                <!-- Slot heading left -->
                <slot name="heading-left" />
              </div>
            </div>
          </div>
        </div>
      </div>
    </div>
  </section>
  <section class="section pt-0">
    <div class="container">
      <div class="columns is-centered">
        <div class="column is-four-fifths">
          <!-- Slot content -->
          <slot name="content" />
          <!-- Slot footer -->
          <slot name="footer" />
        </div>
      </div>
    </div>
  </section>
</template>

<script></script>

<style></style>
