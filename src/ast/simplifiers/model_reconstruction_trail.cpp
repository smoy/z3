/*++
Copyright (c) 2022 Microsoft Corporation

Module Name:

    model_reconstruction_trail.cpp

Author:

    Nikolaj Bjorner (nbjorner) 2022-11-3.
    
--*/


#include "ast/for_each_expr.h"
#include "ast/simplifiers/model_reconstruction_trail.h"
#include "ast/converters/generic_model_converter.h"


void model_reconstruction_trail::replay(dependent_expr const& d, vector<dependent_expr>& added) {
    // accumulate a set of dependent exprs, updating m_trail to exclude loose 
    // substitutions that use variables from the dependent expressions.
    ast_mark free_vars;
    
    add_vars(d, free_vars);

    added.push_back(d);

    for (auto& t : m_trail) {
        if (!t->m_active)
            continue;

        // updates that have no intersections with current variables are skipped
        if (!t->intersects(free_vars)) 
            continue;        

        // loose entries that intersect with free vars are deleted from the trail
        // and their removed formulas are added to the resulting constraints.
        if (t->is_loose()) {
            added.append(t->m_removed); 
            for (auto r : t->m_removed) 
                add_vars(r, free_vars);            
            m_trail_stack.push(value_trail(t->m_active));
            t->m_active = false;      
            continue;
        }
        
        // rigid entries:
        // apply substitution to added in case of rigid model convertions
        for (auto& d : added) {
            auto [f, dep1] = d();
            expr_ref g(m);
            expr_dependency_ref dep2(m);
            (*t->m_replace)(f, g, dep2);
            d = dependent_expr(m, g, m.mk_join(dep1, dep2));
        }    
    }
}

/**
 * retrieve the current model converter corresponding to chaining substitutions from the trail.
 */
model_converter_ref model_reconstruction_trail::get_model_converter() {

    //
    // walk the trail from the back
    // add substitutions from the back to the generic model converter
    // after they have been normalized using a global replace that replaces 
    // substituted variables by their terms.
    //

    scoped_ptr<expr_replacer> rp = mk_default_expr_replacer(m, true);
    scoped_ptr<expr_substitution> subst = alloc(expr_substitution, m, true, false);
    rp->set_substitution(subst.get());
    generic_model_converter_ref mc = alloc(generic_model_converter, m, "dependent-expr-model");
    bool first = true;
    for (unsigned i = m_trail.size(); i-- > 0; ) {
        auto* t = m_trail[i];
        if (!t->m_active)
            continue;

        if (first) {
            first = false;
            for (auto const& [v, def] : t->m_subst->sub()) {
                expr_dependency* dep = t->m_subst->dep(v);
                subst->insert(v, def, dep);
                mc->add(v, def);
            }
            continue;
        }
        expr_dependency_ref new_dep(m);
        expr_ref new_def(m);

        for (auto const& [v, def] : t->m_subst->sub()) {
            rp->operator()(def, new_def, new_dep);
            expr_dependency* dep = t->m_subst->dep(v);
            new_dep = m.mk_join(dep, new_dep);
            subst->insert(v, new_def, new_dep);
            mc->add(v, new_def);
        }

    }
    return model_converter_ref(mc.get());

}

