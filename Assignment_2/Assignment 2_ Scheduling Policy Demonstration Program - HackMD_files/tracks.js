function getCookie(name) {
  var value = '; ' + document.cookie;
  var parts = value.split('; ' + name + '=');
  if (parts.length === 2) return parts.pop().split(';').shift();
}
window.plausible = window.plausible || function() { (window.plausible.q = window.plausible.q || []).push(arguments); }
function prepareUrl(params) {
  var url = new URL(location.href);
  var queryParams = new URLSearchParams(location.search);
  var customUrl = url.protocol + "//" + url.hostname + url.pathname.replace(/\/$/, '');
  for (var paramName of params) {
    var paramValue = queryParams.get(paramName);
    if (paramValue !== null) customUrl = customUrl + '/' + paramName + ':' + paramValue;
  }
  return customUrl
}
function trackPageView() {
  plausible('pageview', {u: prepareUrl(["nav", "tags", "both", "edit", "view", "template", "create-team", "create-paid-team", "signup"]) + window.location.search, props: {user_id: getCookie('userid'), logged_in: getCookie('loginstate')}});
}
trackPageView()
document.addEventListener('mousedown', function (e) {
  if (e.target && e.target.className && e.target.className.indexOf('ui-') !== -1) {
    plausible('ui_click', {props: {class_name: e.target.className, text: e.target.outerText ? e.target.outerText.substr(0, 200) : "", html: e.target.outerText ? e.target.outerHTML.substr(0, 200) : ""}});
  } else if (e.target && e.target.parentElement && e.target.parentElement.className && e.target.parentElement.className.indexOf('ui-') !== -1) {
    plausible('ui_click', {props: {class_name: e.target.parentElement.className, text: e.target.parentElement.outerText ? e.target.parentElement.outerText.substr(0, 200) : "", html: e.target.parentElement.outerText ? e.target.parentElement.outerHTML.substr(0, 200) : ""}});
  }
});
window.addEventListener('popstate', function (e) {
  trackPageView();
});
window.history.pushState = new Proxy(window.history.pushState, {
  apply: (target, thisArg, argArray) => {
    setTimeout(function () {
      trackPageView();
    }, 1);
    return target.apply(thisArg, argArray);
  },
});
!function(t,e){var o,n,p,r;e.__SV||(window.posthog=e,e._i=[],e.init=function(i,s,a){function g(t,e){var o=e.split(".");2==o.length&&(t=t[o[0]],e=o[1]),t[e]=function(){t.push([e].concat(Array.prototype.slice.call(arguments,0)))}}(p=t.createElement("script")).type="text/javascript",p.crossOrigin="anonymous",p.async=!0,p.src=s.api_host.replace(".i.posthog.com","-assets.i.posthog.com")+"/static/array.js",(r=t.getElementsByTagName("script")[0]).parentNode.insertBefore(p,r);var u=e;for(void 0!==a?u=e[a]=[]:a="posthog",u.people=u.people||[],u.toString=function(t){var e="posthog";return"posthog"!==a&&(e+="."+a),t||(e+=" (stub)"),e},u.people.toString=function(){return u.toString(1)+".people (stub)"},o="init Re Cs Fs Pe Rs Ms capture Ve calculateEventProperties Ds register register_once register_for_session unregister unregister_for_session zs getFeatureFlag getFeatureFlagPayload isFeatureEnabled reloadFeatureFlags updateEarlyAccessFeatureEnrollment getEarlyAccessFeatures on onFeatureFlags onSurveysLoaded onSessionId getSurveys getActiveMatchingSurveys renderSurvey canRenderSurvey canRenderSurveyAsync identify setPersonProperties group resetGroups setPersonPropertiesForFlags resetPersonPropertiesForFlags setGroupPropertiesForFlags resetGroupPropertiesForFlags reset get_distinct_id getGroups get_session_id get_session_replay_url alias set_config startSessionRecording stopSessionRecording sessionRecordingStarted captureException loadToolbar get_property getSessionProperty js As createPersonProfile Ns Is Us opt_in_capturing opt_out_capturing has_opted_in_capturing has_opted_out_capturing clear_opt_in_out_capturing Os debug I Ls getPageViewId captureTraceFeedback captureTraceMetric".split(" "),n=0;n<o.length;n++)g(u,o[n]);e._i.push([i,s,a])},e.__SV=1)}(document,window.posthog||[]);
posthog.init('phc_pCib22A3etO9mVsLiMFrqk7mzZsFnSTFG8L15NvC5Kz', {
    api_host: 'https://tracks.hackmd.io',
    ui_host: 'https://tracks.hackmd.io',
    defaults: '2025-05-24',
    person_profiles: 'identified_only',
    loaded: function () {
      setTimeout(function () {
        if (!posthog._isIdentified()) {
          posthog.identify(getCookie('userid'), {user_id: getCookie('userid'), logged_in: getCookie('loginstate')});
        }
      }, 100);
    },
});
