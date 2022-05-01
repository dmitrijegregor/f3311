static void* find_sym( const char *sym ) {  // find address kernel symbol sym
   static unsigned long faddr = 0;          // static !!!
   // ----------- nested functions are a GCC extension ---------
   int symb_fn( void* data, const char* sym, struct module* mod, unsigned long addr ) {
      if( 0 == strcmp( (char*)data, sym ) ) {
         faddr = addr;
         return 1;
      }
      else return 0;
   };
   // --------------------------------------------------------
   kallsyms_on_each_symbol( symb_fn, (void*)sym );
   return (void*)faddr;
}
