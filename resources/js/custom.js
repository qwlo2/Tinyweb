(function ($) {

    "use strict";

        // PRE loader
        $(window).load(function(){
          $('.preloader').fadeOut(1000); // set duration in brackets    
        });


        //Navigation Section
        $('.navbar-collapse a').on('click',function(){
          $(".navbar-collapse").collapse('hide');
        });

        $('.js-logout').on('click', function(){
          var button = this;
          button.disabled = true;

          fetch('/logout', {
            method: 'POST',
            credentials: 'same-origin'
          })
          .then(function(response){
            if (!response.ok) {
              throw new Error('logout failed');
            }
            return response.json();
          })
          .then(function(result){
            if (!result || result.ok !== true) {
              throw new Error('unexpected logout response');
            }
            window.location.href = '/login.html';
          })
          .catch(function(){
            button.disabled = false;
            window.alert('退出失败，请稍后重试。');
          });
        });

        $(window).scroll(function() {
          if ($(".navbar").offset().top > 50) {
            $(".navbar-fixed-top").addClass("top-nav-collapse");
              } else {
                $(".navbar-fixed-top").removeClass("top-nav-collapse");
              }
        });


        // Smoothscroll js
        $(function() {
          $('.custom-navbar a, #home a').bind('click', function(event) {
            var $anchor = $(this);
            $('html, body').stop().animate({
                scrollTop: $($anchor.attr('href')).offset().top - 49
            }, 1000);
            event.preventDefault();
          });
        });  


        // WOW Animation js
        new WOW({ mobile: false }).init();

})(jQuery);
