

#include <thread>
#include <vector>
#include <algorithm>

extern "C" {
#include <xdo.h>
}
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include "payload.hpp"
#include "interface.hpp"

#include "helpers.hpp"

using XWindow_t = Window;

struct CandidateWindowInfo{
  XWindow_t window_id;
  std::string window_name;
  int window_width;
  int window_height;
};

std::vector<CandidateWindowInfo> x11_sanitizer_get_targets(
  Display* display,
  XWindow_t root_window
){
  // hunt for direct children of root window that has override_redirect set to true
  Window root_return, parent_return;
  Window* children_return;
  unsigned int nchildren_return;
  auto xquery_status = XQueryTree(display, root_window, &root_return, &parent_return, &children_return, &nchildren_return);
  if (xquery_status == 0) {
    fprintf(stderr, "%s", red_text("[x11_sanitizer] XQueryTree failed. \n").c_str());
    return {};
  }
  std::vector<CandidateWindowInfo> targets;
  for (int i = 0; i < nchildren_return; i++) {
    XWindow_t cur_window = children_return[i];
    XWindowAttributes window_attributes;
    auto xgetwindow_status = XGetWindowAttributes(display, cur_window, &window_attributes);
    if (xgetwindow_status == 0) {
      fprintf(stderr, "%s", red_text("[x11_sanitizer] XGetWindowAttributes failed. \n").c_str());
      continue;
    }
    if (window_attributes.override_redirect == false) {
      continue;
    }

    // found override_redirect window
    // check its name

    std::string window_name;
    XTextProperty prop;
    Atom wmNameAtom = XInternAtom(display, "_NET_WM_NAME", True);
    if (wmNameAtom && XGetTextProperty(display, cur_window, &prop, wmNameAtom) && prop.value) {
        window_name = std::string(reinterpret_cast<char*>(prop.value));
        XFree(prop.value);
    }

    // check if "wemeet" is in the window name
    if (window_name.find("wemeet") == std::string::npos) {
      continue;
    }


    // found candidate window here, add it to the list

    targets.push_back(CandidateWindowInfo{
      .window_id = cur_window,
      .window_name = window_name,
      .window_width = window_attributes.width,
      .window_height = window_attributes.height
    });
  }
  XFree(children_return);
  return targets;
}

/*
  Below is a *VERY CRAZY* code that get rid of the stupid asshole wemeet screenshare overlay window, which
  prevents the user from clicking anything on the screen.

  we accomplish this by:

  1. First locate the right window to get rid of. This can be very tricky, since wemeet would
  create and destroy multiple windows with override_redirect set to true after you started screenshare.
  If the wrong window is picked, the whole wemeet app GUI would enter a very weird state and will eventually
  crash, LMFAO. However I observed that it would eventually enter a "steady state" where the right window
  would be the one with the largest size. 
  So we wait for $STEADY_WAIT_TIME seconds after the first override_redirect window has been found, and
  pick the right window based on the window size.

  2. After the right window is picked, we do the following operations:
    - set override_redirect to false
    - unmap the window - this would temporarily hide the window
    - map the window - this wakes up window manager(like KWin) to manage it
    - minimize the window * 3 - since 重要的事情要说三遍

  well actually I found that it's sufficient to just minimize the window. so screw it, we just minimize it.
*/

void x11_sanitizer_main()
{

  // get envvar "XDG_SESSION_TYPE" to determine the session type
  // if it's "wayland", then the x11 sanitizer can just exit
  char* xdg_session_type = getenv("XDG_SESSION_TYPE");
  if (xdg_session_type && std::string(xdg_session_type) == "wayland") {
    fprintf(stderr, "%s", green_text("[x11_sanitizer] wayland session detected. skipping x11 sanitizer. \n").c_str());
    return;
  }
  
  auto& interface_singleton = InterfaceSingleton::getSingleton();
  auto* interface_handle = interface_singleton.interface_handle.load();
  Display* display = XOpenDisplay(NULL);
  XWindow_t root_window = DefaultRootWindow(display);
  xdo_t* xdo = xdo_new(NULL);

  // 1.5 seconds steady wait time seems to be "generally safe".
  // A more aggressive value like 1 seconds seems to be too risky.
  std::chrono::milliseconds STEADY_WAIT_TIME(2000);
  std::chrono::time_point<std::chrono::high_resolution_clock> steady_start_time;
  bool target_first_occurred = false;
  bool target_managed = false;
  XWindow_t picked_window_id = 0;

  while(interface_handle->x11_sanitizer_stop_flag.load(std::memory_order_seq_cst) == false){

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // the below code is commented so that
    // we KEEP minimizing this window until the stop signal is received
    // we suppress it as hard as we can to express our anger
    // if (picked_window_id!=0){
    //   continue;
    // }

    auto targets = x11_sanitizer_get_targets(display, root_window);
    auto now = std::chrono::high_resolution_clock::now();
    if (targets.size() != 0 && target_first_occurred == false) {
      target_first_occurred = true;
      steady_start_time = now;
    }
    if (target_first_occurred && (now - steady_start_time) < STEADY_WAIT_TIME) {
      // wait for steady state
      continue;
    }
    if (target_first_occurred && (now - steady_start_time) >= STEADY_WAIT_TIME) {
      
      if (!picked_window_id) {
        fprintf(stderr, "%s", yellow_text("[payload x11 sanitizer] steady wait done. \n").c_str());
        auto comparator = [](const CandidateWindowInfo& a, const CandidateWindowInfo& b) {
          int64_t area_a = static_cast<int64_t>(a.window_width) * a.window_height;
          int64_t area_b = static_cast<int64_t>(b.window_width) * b.window_height;
          return area_a > area_b;
        };

        std::sort(targets.begin(), targets.end(), comparator);
        picked_window_id = targets[0].window_id;
        fprintf(stderr, "%s", yellow_text("[payload x11 sanitizer] picked window: 0x" + int_to_hexstr(picked_window_id) + "\n").c_str());
      }
      
      if (!target_managed) {
        // get it managed by window manager
        // it is all you need on GNOME
        xdo_set_window_override_redirect(
          xdo, picked_window_id, 0
        );
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        xdo_unmap_window(
          xdo, picked_window_id
        );
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        // do it only once to get rid of annoying 'wemeetapp is ready' notifications
        xdo_map_window(
          xdo, picked_window_id
        );
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        target_managed = true;
      }
      
      // This should be commented if test for KDE turns out good.
      // It hides the full-screen window indicating sharing area
      // which does not matter but gives inconsistent user experience.
      // If this window gets maximized, it will shadow gnome panel.
      constexpr int IMPORTANT_THINGS_WORTH_THREE_REITERATIONS = 3;
      for (int i = 0; i < IMPORTANT_THINGS_WORTH_THREE_REITERATIONS; i++) {
        xdo_minimize_window(
          xdo, picked_window_id
        );
      }
    }  
  }
  XCloseDisplay(display);
}




std::thread payload_start_portal_gio_mainloop_thread(){
  auto& interface_singleton = InterfaceSingleton::getSingleton();
  auto* portal_handle = interface_singleton.portal_handle.load();
  // this thread is going to be trapped in the gio mainloop
  std::thread portal_gio_mainloop_thread = std::thread(
    [portal_handle](){
      g_main_loop_run(portal_handle->gio_mainloop);
    }
  );
  // wait until xdpsession is up
  // TODO: this CAN actually fail and trap the program...
  // TODO: but i think it's relatively safe to assume that the session will be up for now
  // TODO: eventually need to deal with this
  while(!portal_handle->session.load()){
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  return std::move(portal_gio_mainloop_thread);
}


std::thread payload_start_pipewire_thread(){
  auto& interface_singleton = InterfaceSingleton::getSingleton();
  auto* pipewire_handle = interface_singleton.pipewire_handle.load();
  // this thread is going to be trapped in the pipewire mainloop
  std::thread pipewire_thread = std::thread(
    [](){
      auto& interface_singleton = InterfaceSingleton::getSingleton();
      while (interface_singleton.interface_handle.load()->pw_stop_flag.load() == false) {
        auto* pipewire_handle = interface_singleton.pipewire_handle.load();
        pw_loop_iterate(pw_main_loop_get_loop(pipewire_handle->pw_mainloop), 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
      }
      fprintf(stderr, "%s", green_text("[payload] pw stop signal received. pw stopped. \n").c_str());
    }
  );
  fprintf(stderr, "%s", green_text("[payload] pipewire thread started.\n").c_str());
  return std::move(pipewire_thread);
}


// this payload_main will be executed by the payload_thread, which is created in XShmAttachHook
// and this thread will be detached immediately after creation
void payload_main(){


  auto& interface_singleton = InterfaceSingleton::getSingleton();
  auto* portal_handle = interface_singleton.portal_handle.load();

  // start the gio mainloop thread
  std::thread portal_gio_mainloop_thread = payload_start_portal_gio_mainloop_thread();

  // start x11 sanitizer thread
  std::thread x11_sanitizer_thread = std::thread(x11_sanitizer_main);

  // start screencast session
  // this will get the pipewire fd into the portal object
  xdp_session_start(
    portal_handle->session.load(), NULL, NULL,
    XdpScreencastPortal::screencast_session_start_cb,
    portal_handle
  );
  
  // wait until pipewire_fd is up
  while(portal_handle->status.load() == XdpScreencastPortalStatus::kInit ) {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  };

  // screencast cancelled. we stop the gio mainloop and join the gio mainloop thread
  if (portal_handle->status.load() == XdpScreencastPortalStatus::kCancelled) {
    fprintf(stderr, "%s", red_text("[payload] screencast cancelled. stop gio and join gio thread. \n").c_str());
    g_main_loop_quit(portal_handle->gio_mainloop);
    portal_gio_mainloop_thread.join();
    return;
  }

  while(portal_handle->pipewire_fd.load() == -1){
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  fprintf(stderr, "%s", green_text("[payload SYNC] pipewire_fd acquired: " + std::to_string(portal_handle->pipewire_fd.load()) + "\n").c_str());

  
  while(interface_singleton.pipewire_handle.load() == nullptr){
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  fprintf(stderr, "%s", green_text("[payload SYNC] got pipewire_handle.\n").c_str());

  // start the pipewire thread
  std::thread pipewire_thread = payload_start_pipewire_thread();

  // we can join the sanitizer thread here since the pipewire thread is up
  // which means the user has successfully started the screenshare
  interface_singleton.interface_handle.load()->x11_sanitizer_stop_flag.store(true, std::memory_order_seq_cst);
  x11_sanitizer_thread.join();
  fprintf(stderr, "%s", green_text("[payload SYNC] x11 sanitizer stopped.\n").c_str());

  pipewire_thread.join();
  interface_singleton.interface_handle.load()->payload_pw_stop_confirm.store(true, std::memory_order_seq_cst);
  fprintf(stderr, "%s", green_text("[payload SYNC] pw stop confirmed.\n").c_str());

  portal_gio_mainloop_thread.join();
  interface_singleton.interface_handle.load()->payload_gio_stop_confirm.store(true, std::memory_order_seq_cst);
  fprintf(stderr, "%s", green_text("[payload SYNC] gio stop confirmed.\n").c_str());

  
  return;

}
