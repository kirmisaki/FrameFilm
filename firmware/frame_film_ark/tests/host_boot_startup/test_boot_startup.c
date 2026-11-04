/* Dispatch the observed pre-config list and the BOOT_DONE-to-SWITCH interleaving. */

int main(void){
 app_manager_init();
 app_entry_t image={.on_enter=image_enter,.on_event=image_event};
 m_app_registry[APP_ID_IMAGE]=&image;
 app_event_t list={.type=APP_EVT_SYS,.cmd=123};
 app_handle_event(&list);
 assert(enters==0 && events==0);
 app_ui_ops_t ops={0};
 app_manager_boot_show(&ops,APP_ID_MENU);
 assert(ui_pages==1);
 app_handle_event(&list);
 assert(enters==0 && events==0);
 app_event_t done={.type=APP_EVT_UI_MSG,.cmd=APP_UI_REQ_BOOT_DONE};
 app_handle_event(&done);
 assert(m_boot_page && switches==0);
 app_handle_event(&list);
 assert(enters==0 && events==0);
 app_handle_event(&queued_switch);
 assert(switches==1 && !m_boot_page && m_current_app==APP_ID_MENU);
 m_boot_page=1;m_current_app=APP_ID_IMAGE;m_app_running=0;
 app_event_t direct={.type=APP_EVT_SWITCH,.data=(void*)(uintptr_t)APP_ID_IMAGE};
 app_handle_event(&direct);
 assert(switches==2 && !m_boot_page);
 puts("PASS: startup subscription/timer/task gated; pre-config list suppressed; boot UI sole owner; BOOT_DONE tail-queue list suppressed; direct first switch allowed");
}
